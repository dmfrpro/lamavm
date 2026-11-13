#include "link.h"

#include "builtins.h"
#include "decode.h"
#include "unit.h"
#include "unit_internal.h"
#include "util.h"

#include <string.h>

static struct pubsym *pub_find(const char *name, uint8_t flag)
{
	struct unit *u;
	struct pubsym *p;
	size_t i, j;

	for (i = 0; i < unit_count(); i++) {
		u = unit_at(i);
		for (j = 0; j < u->pubs_count; j++) {
			p = unit_pub_at(u, j);
			if (p->flag == flag &&
			    strcmp(string_at(u, p->name, 0), name) == 0)
				return p;
		}
	}

	return NULL;
}

static void link_call(struct insn *in)
{
	struct unit *u = unit_get(in->unit_id);
	struct pubsym *p;
	struct insn target;
	const char *name;

	if (in->a >= 0) {
		in->target = (struct code_ref){ in->unit_id, in->a };
		return;
	}

	name = external_string_at(u, in->a, in->raw);
	p = pub_find(name, PUB_FUNCTION);
	if (p) {
		if (in->op == I_CLOSURE) {
			decode_insn(unit_get(p->unit_id), p->entry, &target);
			if (in->b != target.imm)
				die_at(unit_name(u), in->raw,
				       "CLOSURE capture count %d does not match target capture count %d",
				       in->b, (int)target.imm);
		}
		in->target = (struct code_ref){ p->unit_id, p->entry };
		return;
	}

	if (in->op != I_CALL)
		die_at(unit_name(u), in->raw,
		       "undefined external function \"%s\"", name);

	in->bi = builtin_find(name);
	if (!in->bi)
		die_at(unit_name(u), in->raw,
		       "undefined external function \"%s\"", name);
}

static void link_global(struct insn *in)
{
	struct unit *u = unit_get(in->unit_id);
	struct pubsym *p;
	const char *name;

	if (in->kind != K_GLO)
		return;
	if (in->a >= 0) {
		in->glob = u->globals + in->a;
		return;
	}

	name = external_string_at(u, in->a, in->raw);
	p = pub_find(name, PUB_GLOBAL);
	if (!p)
		die_at(unit_name(u), in->raw,
		       "undefined external global \"%s\"", name);

	in->glob = p->glob;
}

void link_capture(const struct insn *in, struct capture *cap)
{
	struct unit *u = unit_get(in->unit_id);
	struct pubsym *p;
	const char *name;

	if (cap->kind != K_GLO)
		return;
	if (cap->index >= 0) {
		cap->glob = u->globals + cap->index;
		return;
	}

	name = external_string_at(u, cap->index, in->raw);
	p = pub_find(name, PUB_GLOBAL);
	if (!p)
		die_at(unit_name(u), in->raw,
		       "undefined external global \"%s\"", name);

	cap->glob = p->glob;
}

void link_insn(struct insn *in)
{
	switch (in->op) {
	case I_CALL:
	case I_CLOSURE:
		link_call(in);
		break;
	case I_JMP:
	case I_CJMPZ:
	case I_CJMPNZ:
		in->target = (struct code_ref){ in->unit_id, in->a };
		break;
	case I_LD_GLO:
	case I_LDA:
	case I_ST_GLO:
		link_global(in);
		break;
	default:
		break;
	}
}

static void link_references(struct insn *in)
{
	struct capture cap;
	int32_t k;

	link_insn(in);
	if (in->op != I_CLOSURE)
		return;

	for (k = 0; k < in->b; k++) {
		decode_capture(in, k, &cap);
		link_capture(in, &cap);
	}
}

void link_units(void)
{
	struct unit *u;
	struct insn in;
	size_t pos;
	size_t i, j;

	for (i = 0; i < unit_count(); i++) {
		u = unit_at(i);
		for (j = 0; j < u->pubs_count; j++)
			if (unit_pub_at(u, j)->flag == PUB_GLOBAL)
				unit_pub_at(u, j)->glob =
					u->globals + unit_pub_at(u, j)->offset;
	}

	for (i = 0; i < unit_count(); i++) {
		u = unit_at(i);
		for (pos = 0; pos < u->code_size;
		     pos = (size_t)in.next.offset) {
			decode_insn(u, (int32_t)pos, &in);
			link_references(&in);
		}
	}
}

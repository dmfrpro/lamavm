#include "link.h"

#include "builtins.h"
#include "decode.h"
#include "unit.h"
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
			if (p->flag == flag && strcmp(p->name, name) == 0)
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

	if (!in->ext_name) {
		in->target = u->code + in->a;
		return;
	}

	p = pub_find(in->ext_name, PUB_FUNCTION);
	if (p) {
		if (in->op == I_CLOSURE) {
			decode_insn(unit_of_code(p->entry), p->entry, &target);
			if (in->b != target.imm)
				die_at(unit_name(u), in->raw,
				       "CLOSURE capture count %d does not match target capture count %d",
				       in->b, (int)target.imm);
		}
		in->target = p->entry;
		return;
	}

	if (in->op != I_CALL)
		die_at(unit_name(u), in->raw,
		       "undefined external function \"%s\"", in->ext_name);

	in->bi = builtin_find(in->ext_name);
	if (!in->bi)
		die_at(unit_name(u), in->raw,
		       "undefined external function \"%s\"", in->ext_name);
}

static void link_global(struct insn *in)
{
	struct unit *u = unit_get(in->unit_id);
	struct pubsym *p;

	if (in->kind != K_GLO)
		return;
	if (!in->ext_name) {
		in->glob = u->globals + in->a;
		return;
	}

	p = pub_find(in->ext_name, PUB_GLOBAL);
	if (!p)
		die_at(unit_name(u), in->raw,
		       "undefined external global \"%s\"", in->ext_name);

	in->glob = p->glob;
}

void link_capture(const struct insn *in, struct capture *cap)
{
	struct pubsym *p;

	if (cap->kind != K_GLO)
		return;
	if (!cap->ext_name) {
		cap->glob = unit_get(in->unit_id)->globals + cap->index;
		return;
	}

	p = pub_find(cap->ext_name, PUB_GLOBAL);
	if (!p)
		die_at(unit_name(unit_get(in->unit_id)), in->raw,
		       "undefined external global \"%s\"", cap->ext_name);

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
		in->target = unit_get(in->unit_id)->code + in->a;
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

static void validate_external_refs(struct insn *in)
{
	struct capture cap;
	int32_t k;

	if (in->ext_name)
		link_insn(in);
	if (in->op != I_CLOSURE)
		return;

	for (k = 0; k < in->b; k++) {
		decode_capture(in, k, &cap);
		if (cap.ext_name)
			link_capture(in, &cap);
	}
}

void link_units(void)
{
	struct unit *u;
	struct insn in;
	const uint8_t *code;
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
		for (code = u->code; code < u->code + u->code_size;
		     code = in.next) {
			decode_insn(u, code, &in);
			validate_external_refs(&in);
		}
	}
}

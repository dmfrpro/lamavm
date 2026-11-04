#include "link.h"

#include "builtins.h"
#include "unit.h"
#include "util.h"

#include <string.h>

static struct pubsym *pub_find(const char *name, uint8_t flag)
{
	struct pubsym *p;
	size_t i, j;

	for (i = 0; i < units_count; i++)
		for (j = 0; j < units[i]->pubs_count; j++) {
			p = &units[i]->pubs[j];
			if (p->flag == flag && strcmp(p->name, name) == 0)
				return p;
		}

	return NULL;
}

static void link_call(struct unit *u, struct insn *in)
{
	struct pubsym *p;

	if (!in->ext_name)
		return;

	p = pub_find(in->ext_name, PUB_FUNCTION);
	if (p) {
		in->target = p->entry;
		return;
	}

	if (in->op != I_CALL)
		die_at(u->name, in->raw, "undefined external function \"%s\"",
		       in->ext_name);

	in->bi = builtin_find(in->ext_name);
	if (!in->bi)
		die_at(u->name, in->raw, "undefined external function \"%s\"",
		       in->ext_name);
}

static void link_global(struct unit *u, struct insn *in)
{
	struct pubsym *p;

	if (in->kind != K_GLO)
		return;
	if (!in->ext_name) {
		in->glob = u->globals + in->a;
		return;
	}

	p = pub_find(in->ext_name, PUB_GLOBAL);
	if (!p)
		die_at(u->name, in->raw, "undefined external global \"%s\"",
		       in->ext_name);

	in->glob = p->glob;
}

static void link_capture(struct unit *u, struct insn *in, struct capture *c)
{
	struct pubsym *p;

	if (c->kind != K_GLO)
		return;
	if (!c->ext_name) {
		c->glob = u->globals + c->index;
		return;
	}

	p = pub_find(c->ext_name, PUB_GLOBAL);
	if (!p)
		die_at(u->name, in->raw, "undefined external global \"%s\"",
		       c->ext_name);

	c->glob = p->glob;
}

void link_units(void)
{
	struct unit *u;
	struct insn *in;
	size_t i, j;
	int32_t k;

	for (i = 0; i < units_count; i++) {
		u = units[i];
		for (j = 0; j < u->insns_count; j++) {
			in = &u->insns[j];
			switch (in->op) {
			case I_CALL:
			case I_CLOSURE:
				link_call(u, in);
				break;
			case I_LD:
			case I_LDA:
			case I_ST:
				link_global(u, in);
				break;
			default:
				break;
			}

			if (in->op == I_CLOSURE && in->caps)
				for (k = 0; k < in->b; k++)
					link_capture(u, in, &in->caps[k]);
		}

		for (j = 0; j < u->pubs_count; j++)
			if (u->pubs[j].flag == PUB_GLOBAL)
				u->pubs[j].glob =
					u->globals + u->pubs[j].offset;
	}
}

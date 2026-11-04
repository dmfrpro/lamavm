#include "decode.h"

#include "format.h"
#include "unit_internal.h"
#include "util.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
	INVALID_INDEX = -1,
	INITIAL_INSN_CAPACITY = 256,
};

struct code_reader {
	struct unit *unit;
	size_t pos;
	size_t start;
	uint8_t opcode;
};

struct decoder {
	struct code_reader reader;
	size_t *fn_ids;
	size_t cur_fn;
	size_t index;
	int32_t cur_nargs;
	int32_t cur_nlocals;
	int32_t cur_ncaps;
};

static inline void reader_need(struct code_reader *reader, size_t count)
{
	if (reader->pos + count > reader->unit->code_size)
		die_at(reader->unit->name, reader->start,
		       "truncated opcode 0x%02X: need %d bytes", reader->opcode,
		       (int)count);
}

static inline int32_t reader_i32(struct code_reader *reader)
{
	int32_t value;

	reader_need(reader, BC_I32_SIZE);
	value = rd_i32(reader->unit->code + reader->pos);
	reader->pos += BC_I32_SIZE;
	return value;
}

static void decode_var(struct decoder *d, struct insn *in)
{
	struct code_reader *reader = &d->reader;
	struct unit *u = reader->unit;
	int32_t v, bound;
	uint8_t kind, group;

	v = reader_i32(reader);
	kind = reader->opcode & BC_VAR_KIND_MASK;
	group = reader->opcode & BC_VAR_GROUP_MASK;
	in->op = group == BC_LD_FIRST  ? I_LD :
		 group == BC_LDA_GROUP ? I_LDA :
					 I_ST;
	in->kind = kind;
	in->a = v;

	if (kind != K_GLO) {
		if (v < 0)
			die_at(u->name, reader->start, "negative %s index %d",
			       kind == K_LOC ? "local" :
			       kind == K_ARG ? "argument" :
					       "capture",
			       v);

		bound = kind == K_LOC ? d->cur_nlocals :
			kind == K_ARG ? d->cur_nargs :
					d->cur_ncaps;
		if (bound >= 0 && v >= bound)
			die_at(u->name, reader->start,
			       "%s index %d is out of range [0, %d)",
			       kind == K_LOC ? "local" :
			       kind == K_ARG ? "argument" :
					       "capture",
			       v, bound);
	} else if (v >= 0 && (size_t)v >= u->globals_count) {
		die_at(u->name, reader->start,
		       "global index %d is out of range [0, %zu)", v,
		       u->globals_count);
	} else if (v < 0) {
		in->ext_name = external_string_at(u, v, reader->start);
	}
}

static void decode_begin(struct decoder *d, struct insn *in)
{
	struct code_reader *reader = &d->reader;
	int32_t na, nl, nc;

	na = reader_i32(reader);
	nl = reader_i32(reader);
	if (na < 0)
		die_at(reader->unit->name, reader->start,
		       "BEGIN with negative argument count %d", na);
	if (nl < 0)
		die_at(reader->unit->name, reader->start,
		       "BEGIN with negative local count %d", nl);

	in->op = reader->opcode == BC_BEGIN ? I_BEGIN : I_BEGINC;
	in->a = na;
	in->b = nl;
	d->cur_nargs = na;
	d->cur_nlocals = nl;
	d->cur_ncaps = 0;
	if (reader->opcode == BC_BEGINC) {
		nc = reader_i32(reader);
		if (nc < 0)
			die_at(reader->unit->name, reader->start,
			       "BEGIN_CLOSURE with negative capture count %d",
			       nc);

		in->imm = nc;
		d->cur_ncaps = nc;
	}

	d->cur_fn = (int32_t)d->index;
	d->fn_ids[d->index] = d->cur_fn;
}

static void decode_closure(struct decoder *d, struct insn *in)
{
	struct code_reader *reader = &d->reader;
	struct unit *u = reader->unit;
	int32_t t, nc, v, bound;
	int32_t capture_idx;
	uint8_t kind;

	t = reader_i32(reader);
	nc = reader_i32(reader);
	if (nc < 0)
		die_at(u->name, reader->start,
		       "CLOSURE with negative capture count %d", nc);

	in->op = I_CLOSURE;
	in->a = t;
	in->b = nc;
	if (t < 0)
		in->ext_name = external_string_at(u, t, reader->start);

	in->caps = xmalloc((size_t)nc * sizeof(*in->caps));
	for (capture_idx = 0; capture_idx < nc; capture_idx++) {
		reader_need(reader, 1);
		kind = u->code[reader->pos++];
		v = reader_i32(reader);
		if (kind > K_CLO)
			die_at(u->name, reader->start,
			       "CLOSURE capture %d has invalid kind %u",
			       capture_idx, kind);

		in->caps[capture_idx].kind = kind;
		in->caps[capture_idx].index = v;
		if (kind != K_GLO) {
			if (v < 0)
				die_at(u->name, reader->start,
				       "CLOSURE capture %d has negative index %d",
				       capture_idx, v);

			bound = kind == K_LOC ? d->cur_nlocals :
				kind == K_ARG ? d->cur_nargs :
						d->cur_ncaps;
			if (bound >= 0 && v >= bound)
				die_at(u->name, reader->start,
				       "CLOSURE capture %d index %d is out of range [0, %d)",
				       capture_idx, v, bound);
		} else if (v >= 0 && (size_t)v >= u->globals_count) {
			die_at(u->name, reader->start,
			       "CLOSURE capture %d global index %d is out of range [0, %zu)",
			       capture_idx, v, u->globals_count);
		} else if (v < 0) {
			in->caps[capture_idx].ext_name =
				external_string_at(u, v, reader->start);
		}
	}
}

static void decode_instruction(struct decoder *d, struct insn *in)
{
	struct code_reader *reader = &d->reader;
	struct unit *u = reader->unit;
	size_t start = reader->start;
	uint8_t opc = reader->opcode;
	int32_t v, s, cnt, na, l, c, t;

	switch (opc) {
	case BC_BINOP_FIRST ... BC_BINOP_LAST:
		in->op = I_BINOP;
		in->kind = opc;
		break;
	case BC_CONST:
		v = reader_i32(reader);
		in->op = I_CONST;
		in->imm = BOX(v);
		break;
	case BC_STRING:
		v = reader_i32(reader);
		in->op = I_STRING;
		in->str = string_at(u, v, start);
		break;
	case BC_SEXP:
		s = reader_i32(reader);
		cnt = reader_i32(reader);
		if (cnt < 0)
			die_at(u->name, start,
			       "SEXP with negative field count %d", cnt);

		in->op = I_SEXP;
		in->str = string_at(u, s, start);
		in->imm = LtagHash((char *)in->str);
		in->a = cnt;
		break;
	case BC_STI:
		in->op = I_STI;
		break;
	case BC_STA:
		in->op = I_STA;
		break;
	case BC_JMP:
		v = reader_i32(reader);
		in->op = I_JMP;
		in->a = v;
		break;
	case BC_END:
		in->op = I_END;
		break;
	case BC_RET:
		in->op = I_RET;
		break;
	case BC_DROP:
		in->op = I_DROP;
		break;
	case BC_DUP:
		in->op = I_DUP;
		break;
	case BC_SWAP:
		in->op = I_SWAP;
		break;
	case BC_ELEM:
		in->op = I_ELEM;
		break;
	case BC_LD_FIRST ... BC_LD_LAST:
	case BC_LDA_FIRST ... BC_LDA_FIRST + K_CLO:
	case BC_ST_FIRST ... BC_ST_FIRST + K_CLO:
		decode_var(d, in);
		break;
	case BC_CJMPZ:
	case BC_CJMPNZ:
		v = reader_i32(reader);
		in->op = opc == BC_CJMPZ ? I_CJMPZ : I_CJMPNZ;
		in->a = v;
		break;
	case BC_BEGIN:
	case BC_BEGINC:
		decode_begin(d, in);
		break;
	case BC_CLOSURE:
		decode_closure(d, in);
		break;
	case BC_CALLC:
		v = reader_i32(reader);
		if (v < 0)
			die_at(u->name, start,
			       "CALLC with negative argument count %d", v);

		in->op = I_CALLC;
		in->a = v;
		break;
	case BC_CALL:
		t = reader_i32(reader);
		na = reader_i32(reader);
		if (na < 0)
			die_at(u->name, start,
			       "CALL with negative argument count %d", na);

		in->op = I_CALL;
		in->a = t;
		in->b = na;
		if (t < 0)
			in->ext_name = external_string_at(u, t, start);
		break;
	case BC_TAG:
		s = reader_i32(reader);
		cnt = reader_i32(reader);
		if (cnt < 0)
			die_at(u->name, start,
			       "TAG with negative field count %d", cnt);

		in->op = I_TAG;
		in->str = string_at(u, s, start);
		in->imm = LtagHash((char *)in->str);
		in->a = cnt;
		break;
	case BC_ARRAY:
		v = reader_i32(reader);
		if (v < 0)
			die_at(u->name, start, "ARRAY with negative length %d",
			       v);

		in->op = I_ARRAY;
		in->a = v;
		break;
	case BC_FAIL:
	case BC_FAILC:
		l = reader_i32(reader);
		c = reader_i32(reader);
		in->op = I_FAIL;
		in->a = l;
		in->b = c;
		in->kind = opc == BC_FAILC;
		break;
	case BC_LINE:
		v = reader_i32(reader);
		in->op = I_LINE;
		in->a = v;
		break;
	case BC_PATT_FIRST ... BC_PATT_LAST:
		in->op = I_PATT;
		in->kind = opc - BC_PATT_FIRST;
		break;
	case BC_BARRAY:
		v = reader_i32(reader);
		if (v < 0)
			die_at(u->name, start,
			       "BARRAY with negative element count %d", v);

		in->op = I_BARRAY;
		in->a = v;
		break;
	case BC_EOF:
		in->op = I_EOF;
		if (reader->pos != u->code_size)
			die_at(u->name, start,
			       "unexpected bytes after the EOF opcode (%zu bytes)",
			       u->code_size - reader->pos);
		break;
	default:
		die_at(u->name, start, "unknown opcode 0x%02X", opc);
	}
}

static void resolve_targets(struct unit *u, const size_t *fn_ids)
{
	struct insn *in, *tgt;
	size_t n = u->code_size;
	size_t i;
	int32_t t, ti;
	int ok;

	for (i = 0; i < u->insns_count; i++) {
		in = &u->insns[i];
		if (in->op != I_JMP && in->op != I_CJMPZ &&
		    in->op != I_CJMPNZ && in->op != I_CALL &&
		    in->op != I_CLOSURE)
			continue;
		if (in->ext_name)
			continue;

		t = in->a;
		if (t < 0 || (size_t)t >= n - 1)
			die_at(u->name, in->raw,
			       "control-flow target %d is outside the code section",
			       t);

		ti = u->offset_map[t];
		if (ti < 0)
			die_at(u->name, in->raw,
			       "control-flow target %d is not an instruction boundary",
			       t);

		tgt = &u->insns[ti];
		if (in->op == I_CALL || in->op == I_CLOSURE) {
			ok = in->op == I_CALL ?
				     tgt->op == I_BEGIN :
				     tgt->op == I_BEGIN || tgt->op == I_BEGINC;
			if (!ok)
				die_at(u->name, in->raw,
				       "%s target %d does not point to a function entry (BEGIN%s)",
				       in->op == I_CALL ? "CALL" : "CLOSURE", t,
				       in->op == I_CALL ? "" :
							  " or BEGIN_CLOSURE");
		} else if (fn_ids[i] != fn_ids[ti]) {
			die_at(u->name, in->raw,
			       "jump target %d is outside the enclosing function body",
			       t);
		}

		in->target = tgt;
	}
}

static void resolve_public_entries(struct unit *u)
{
	struct pubsym *p;
	struct insn *e;
	size_t i;
	int32_t ti;

	for (i = 0; i < u->pubs_count; i++) {
		p = &u->pubs[i];
		if (p->flag != PUB_FUNCTION)
			continue;

		ti = u->offset_map[p->offset];
		if (ti < 0)
			die("%s: function \"%s\" offset %d is not a boundary",
			    u->name, p->name, p->offset);

		e = &u->insns[ti];
		if (e->op != I_BEGIN && e->op != I_BEGINC)
			die("%s: function \"%s\" offset %d is not BEGIN",
			    u->name, p->name, p->offset);

		p->entry = e;
	}
}

void unit_decode(struct unit *u)
{
	struct decoder d;
	struct insn *in;
	size_t cap = INITIAL_INSN_CAPACITY;
	size_t n = u->code_size;
	size_t idx, i;

	memset(&d, 0, sizeof(d));
	d.reader.unit = u;
	d.cur_fn = SIZE_MAX;
	d.cur_nargs = INVALID_INDEX;
	d.cur_nlocals = INVALID_INDEX;
	d.cur_ncaps = INVALID_INDEX;

	u->offset_map = xmalloc(n * sizeof(*u->offset_map));
	for (i = 0; i < n; i++)
		u->offset_map[i] = INVALID_INDEX;

	u->insns = xmalloc(cap * sizeof(*u->insns));
	d.fn_ids = xmalloc(cap * sizeof(*d.fn_ids));

	while (d.reader.pos < n) {
		d.reader.start = d.reader.pos;
		d.reader.opcode = u->code[d.reader.pos++];
		if (u->insns_count == cap) {
			cap *= ARRAY_GROWTH_FACTOR;
			u->insns = xrealloc(u->insns, cap * sizeof(*u->insns));
			d.fn_ids = xrealloc(d.fn_ids, cap * sizeof(*d.fn_ids));
		}

		idx = u->insns_count;
		d.index = idx;
		in = &u->insns[idx];
		memset(in, 0, sizeof(*in));
		in->raw = (int32_t)d.reader.start;
		in->unit = u;
		in->name = u->name;
		u->offset_map[d.reader.start] = (int32_t)idx;
		d.fn_ids[idx] = d.cur_fn;
		decode_instruction(&d, in);
		u->insns_count++;
	}

	if (u->insns_count == 0 || u->insns[u->insns_count - 1].op != I_EOF)
		die("%s: code section does not terminate with EOF", u->name);

	resolve_targets(u, d.fn_ids);
	resolve_public_entries(u);
	free(d.fn_ids);
}

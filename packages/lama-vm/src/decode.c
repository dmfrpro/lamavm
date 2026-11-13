#include "decode.h"

#include "execute.h"
#include "format.h"
#include "unit.h"
#include "unit_internal.h"
#include "util.h"

#include <stdint.h>
#include <string.h>

enum { INVALID_INDEX = -1 };

struct code_reader {
	struct unit *unit;
	size_t pos;
	size_t start;
	uint8_t opcode;
};

struct decoder {
	struct code_reader reader;
	int32_t cur_nargs;
	int32_t cur_nlocals;
	int32_t cur_ncaps;
};

static inline void reader_need(struct code_reader *reader, size_t count)
{
	if (reader->pos + count > reader->unit->code_size)
		die_at(unit_name(reader->unit), reader->start,
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
	if (group == BC_LD_FIRST)
		in->op = I_LD_GLO + kind;
	else if (group == BC_LDA_GROUP)
		in->op = I_LDA;
	else
		in->op = I_ST_GLO + kind;
	in->kind = kind;
	in->a = v;

	if (kind != K_GLO) {
		if (v < 0)
			die_at(unit_name(u), reader->start,
			       "negative %s index %d",
			       kind == K_LOC ? "local" :
			       kind == K_ARG ? "argument" :
					       "capture",
			       v);

		bound = kind == K_LOC ? d->cur_nlocals :
			kind == K_ARG ? d->cur_nargs :
					d->cur_ncaps;
		if (bound >= 0 && v >= bound)
			die_at(unit_name(u), reader->start,
			       "%s index %d is out of range [0, %d)",
			       kind == K_LOC ? "local" :
			       kind == K_ARG ? "argument" :
					       "capture",
			       v, bound);
	} else if (v >= 0 && (size_t)v >= u->globals_count) {
		die_at(unit_name(u), reader->start,
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
		die_at(unit_name(reader->unit), reader->start,
		       "BEGIN with negative argument count %d", na);
	if (nl < 0)
		die_at(unit_name(reader->unit), reader->start,
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
			die_at(unit_name(reader->unit), reader->start,
			       "BEGIN_CLOSURE with negative capture count %d",
			       nc);

		in->imm = nc;
		d->cur_ncaps = nc;
	}
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
		die_at(unit_name(u), reader->start,
		       "CLOSURE with negative capture count %d", nc);

	in->op = I_CLOSURE;
	in->a = t;
	in->b = nc;
	in->caps = u->code + reader->pos;
	if (t < 0)
		in->ext_name = external_string_at(u, t, reader->start);

	for (capture_idx = 0; capture_idx < nc; capture_idx++) {
		reader_need(reader, 1);
		kind = u->code[reader->pos++];
		v = reader_i32(reader);
		if (kind > K_CLO)
			die_at(unit_name(u), reader->start,
			       "CLOSURE capture %d has invalid kind %u",
			       capture_idx, kind);

		if (kind != K_GLO) {
			if (v < 0)
				die_at(unit_name(u), reader->start,
				       "CLOSURE capture %d has negative index %d",
				       capture_idx, v);

			bound = kind == K_LOC ? d->cur_nlocals :
				kind == K_ARG ? d->cur_nargs :
						d->cur_ncaps;
			if (bound >= 0 && v >= bound)
				die_at(unit_name(u), reader->start,
				       "CLOSURE capture %d index %d is out of range [0, %d)",
				       capture_idx, v, bound);
		} else if (v >= 0 && (size_t)v >= u->globals_count) {
			die_at(unit_name(u), reader->start,
			       "CLOSURE capture %d global index %d is out of range [0, %zu)",
			       capture_idx, v, u->globals_count);
		} else if (v < 0) {
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
		in->op = I_BINOP_ADD + opc - BC_BINOP_ADD;
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
			die_at(unit_name(u), start,
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
			die_at(unit_name(u), start,
			       "CALLC with negative argument count %d", v);

		in->op = I_CALLC;
		in->a = v;
		break;
	case BC_CALL:
		t = reader_i32(reader);
		na = reader_i32(reader);
		if (na < 0)
			die_at(unit_name(u), start,
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
			die_at(unit_name(u), start,
			       "TAG with negative field count %d", cnt);

		in->op = I_TAG;
		in->str = string_at(u, s, start);
		in->imm = LtagHash((char *)in->str);
		in->a = cnt;
		break;
	case BC_ARRAY:
		v = reader_i32(reader);
		if (v < 0)
			die_at(unit_name(u), start,
			       "ARRAY with negative length %d", v);

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
			die_at(unit_name(u), start,
			       "BARRAY with negative element count %d", v);

		in->op = I_BARRAY;
		in->a = v;
		break;
	case BC_EOF:
		in->op = I_EOF;
		if (reader->pos != u->code_size)
			die_at(unit_name(u), start,
			       "unexpected bytes after the EOF opcode (%zu bytes)",
			       u->code_size - reader->pos);
		break;
	default:
		die_at(unit_name(u), start, "unknown opcode 0x%02X", opc);
	}
}

static void decode_reader(struct decoder *d, struct insn *in)
{
	memset(in, 0, sizeof(*in));
	d->reader.start = d->reader.pos;
	d->reader.opcode = d->reader.unit->code[d->reader.pos++];
	in->raw = (int32_t)d->reader.start;
	in->unit_id = d->reader.unit->id;
	decode_instruction(d, in);
	in->next = d->reader.unit->code + d->reader.pos;
	in->handler = execute_handler(in->op);
}

static void decoder_init(struct decoder *d, struct unit *u, size_t pos)
{
	memset(d, 0, sizeof(*d));
	d->reader.unit = u;
	d->reader.pos = pos;
	d->cur_nargs = INVALID_INDEX;
	d->cur_nlocals = INVALID_INDEX;
	d->cur_ncaps = INVALID_INDEX;
}

void decode_insn(struct unit *u, const uint8_t *code, struct insn *in)
{
	struct decoder d;

	if (code < u->code || code >= u->code + u->code_size)
		die("%s: instruction pointer is outside the code section",
		    unit_name(u));
	decoder_init(&d, u, (size_t)(code - u->code));
	decode_reader(&d, in);
}

void decode_capture(const struct insn *in, int32_t index, struct capture *cap)
{
	const uint8_t *p = in->caps + (size_t)index * (1 + BC_I32_SIZE);

	memset(cap, 0, sizeof(*cap));
	cap->kind = p[0];
	cap->index = rd_i32(p + 1);
	if (cap->kind == K_GLO && cap->index < 0)
		cap->ext_name = external_string_at(unit_get(in->unit_id),
						   cap->index, in->raw);
}

const uint8_t *decode_find(struct unit *u, int32_t offset, struct insn *in)
{
	const uint8_t *code;

	if (offset < 0 || (size_t)offset >= u->code_size - 1)
		return NULL;
	for (code = u->code; code < u->code + u->code_size;) {
		decode_insn(u, code, in);
		if (code == u->code + offset)
			return code;
		code = in->next;
	}
	return NULL;
}

static size_t function_owner(struct unit *u, int32_t offset)
{
	struct insn in;
	size_t fn = SIZE_MAX;
	int32_t pos;

	for (pos = 0; pos <= offset;) {
		decode_insn(u, u->code + pos, &in);
		if (in.op == I_BEGIN || in.op == I_BEGINC)
			fn = (size_t)pos;
		if (pos == offset)
			break;
		pos = in.next - u->code;
	}
	return fn;
}

static void resolve_target(struct unit *u, struct insn *in)
{
	struct insn target;
	size_t target_fn;

	in->target = decode_find(u, in->a, &target);
	if (!in->target)
		die_at(unit_name(u), in->raw,
		       "control-flow target %d is not an instruction boundary",
		       in->a);

	if (in->op == I_CALL || in->op == I_CLOSURE) {
		if (target.op != I_BEGIN &&
		    (in->op != I_CLOSURE || target.op != I_BEGINC))
			die_at(unit_name(u), in->raw,
			       "%s target %d does not point to a function entry (BEGIN%s)",
			       in->op == I_CALL ? "CALL" : "CLOSURE", in->a,
			       in->op == I_CALL ? "" : " or BEGIN_CLOSURE");
		if (in->op == I_CLOSURE && in->b != target.imm)
			die_at(unit_name(u), in->raw,
			       "CLOSURE capture count %d does not match target capture count %d",
			       in->b, (int)target.imm);
	} else {
		target_fn = function_owner(u, in->a);
		if (function_owner(u, in->raw) != target_fn)
			die_at(unit_name(u), in->raw,
			       "jump target %d is outside the enclosing function body",
			       in->a);
	}
}

void unit_validate(struct unit *u)
{
	struct decoder d;
	struct insn in = { 0 };
	struct pubsym *p;
	size_t i;

	decoder_init(&d, u, 0);
	while (d.reader.pos < u->code_size) {
		decode_reader(&d, &in);
		if ((in.op == I_JMP || in.op == I_CJMPZ || in.op == I_CJMPNZ ||
		     in.op == I_CALL || in.op == I_CLOSURE) &&
		    !in.ext_name)
			resolve_target(u, &in);
	}
	if (in.op != I_EOF)
		die("%s: code section does not terminate with EOF",
		    unit_name(u));

	for (i = 0; i < u->pubs_count; i++) {
		p = unit_pub_at(u, i);
		if (p->flag != PUB_FUNCTION)
			continue;
		p->entry = decode_find(u, p->offset, &in);
		if (!p->entry)
			die("%s: function \"%s\" offset %d is not a boundary",
			    unit_name(u), p->name, p->offset);
		if (in.op != I_BEGIN && in.op != I_BEGINC)
			die("%s: function \"%s\" offset %d is not BEGIN",
			    unit_name(u), p->name);
	}
	u->validated = 1;
}

#include "decode.h"

#include "execute.h"
#include "format.h"
#include "meta.h"
#include "reader.h"
#include "unit.h"
#include "unit_internal.h"
#include "util.h"

#include <stdint.h>
#include <string.h>

enum { INVALID_INDEX = -1, INVALID_BOUNDARY = INT32_MIN };

struct decoder {
	struct byte_reader reader;
	uint8_t opcode;
	size_t start;
	int32_t cur_nargs;
	int32_t cur_nlocals;
	int32_t cur_ncaps;
};

static void decode_var(struct decoder *d, struct insn *in)
{
	struct byte_reader *reader = &d->reader;
	struct unit *u = unit_get(reader->unit_id);
	int32_t v, bound;
	uint8_t kind, group;

	v = reader_i32(reader);
	kind = d->opcode & BC_VAR_KIND_MASK;
	group = d->opcode & BC_VAR_GROUP_MASK;
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
			die_at(unit_name(u), d->start, "negative %s index %d",
			       kind == K_LOC ? "local" :
			       kind == K_ARG ? "argument" :
					       "capture",
			       v);

		bound = kind == K_LOC ? d->cur_nlocals :
			kind == K_ARG ? d->cur_nargs :
					d->cur_ncaps;
		if (bound >= 0 && v >= bound)
			die_at(unit_name(u), d->start,
			       "%s index %d is out of range [0, %d)",
			       kind == K_LOC ? "local" :
			       kind == K_ARG ? "argument" :
					       "capture",
			       v, bound);
	} else if (v >= 0 && (size_t)v >= u->globals_count) {
		die_at(unit_name(u), d->start,
		       "global index %d is out of range [0, %zu)", v,
		       u->globals_count);
	} else if (v < 0) {
		(void)external_string_at(u, v, d->start);
	}
}

static void decode_begin(struct decoder *d, struct insn *in)
{
	struct byte_reader *reader = &d->reader;
	int32_t na, nl, nc;

	na = reader_i32(reader);
	nl = reader_i32(reader);
	if (na < 0)
		die_at(unit_name(unit_get(reader->unit_id)), d->start,
		       "BEGIN with negative argument count %d", na);
	if (nl < 0)
		die_at(unit_name(unit_get(reader->unit_id)), d->start,
		       "BEGIN with negative local count %d", nl);

	in->op = d->opcode == BC_BEGIN ? I_BEGIN : I_BEGINC;
	in->a = na;
	in->b = nl;
	d->cur_nargs = na;
	d->cur_nlocals = nl;
	d->cur_ncaps = 0;
	if (d->opcode == BC_BEGINC) {
		nc = reader_i32(reader);
		if (nc < 0)
			die_at(unit_name(unit_get(reader->unit_id)), d->start,
			       "BEGIN_CLOSURE with negative capture count %d",
			       nc);

		in->imm = nc;
		d->cur_ncaps = nc;
	}
}

static void decode_closure(struct decoder *d, struct insn *in)
{
	struct byte_reader *reader = &d->reader;
	struct unit *u = unit_get(reader->unit_id);
	int32_t t, nc, v, bound;
	int32_t capture_idx;
	uint8_t kind;

	t = reader_i32(reader);
	nc = reader_i32(reader);
	if (nc < 0)
		die_at(unit_name(u), d->start,
		       "CLOSURE with negative capture count %d", nc);

	in->op = I_CLOSURE;
	in->a = t;
	in->b = nc;
	in->caps = reader->pos;
	if (t < 0)
		(void)external_string_at(u, t, d->start);

	for (capture_idx = 0; capture_idx < nc; capture_idx++) {
		kind = reader_u8(reader);
		v = reader_i32(reader);
		if (kind > K_CLO)
			die_at(unit_name(u), d->start,
			       "CLOSURE capture %d has invalid kind %u",
			       capture_idx, kind);

		if (kind != K_GLO) {
			if (v < 0)
				die_at(unit_name(u), d->start,
				       "CLOSURE capture %d has negative index %d",
				       capture_idx, v);

			bound = kind == K_LOC ? d->cur_nlocals :
				kind == K_ARG ? d->cur_nargs :
						d->cur_ncaps;
			if (bound >= 0 && v >= bound)
				die_at(unit_name(u), d->start,
				       "CLOSURE capture %d index %d is out of range [0, %d)",
				       capture_idx, v, bound);
		} else if (v >= 0 && (size_t)v >= u->globals_count) {
			die_at(unit_name(u), d->start,
			       "CLOSURE capture %d global index %d is out of range [0, %zu)",
			       capture_idx, v, u->globals_count);
		} else if (v < 0) {
			(void)external_string_at(u, v, d->start);
		}
	}
}

static void decode_instruction(struct decoder *d, struct insn *in)
{
	struct byte_reader *reader = &d->reader;
	struct unit *u = unit_get(reader->unit_id);
	size_t start = d->start;
	uint8_t opc = d->opcode;
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
		in->str = v;
		(void)string_at(u, v, start);
		break;
	case BC_SEXP:
		s = reader_i32(reader);
		cnt = reader_i32(reader);
		if (cnt < 0)
			die_at(unit_name(u), start,
			       "SEXP with negative field count %d", cnt);

		in->op = I_SEXP;
		in->str = s;
		in->imm = LtagHash((char *)string_at(u, s, start));
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
			(void)external_string_at(u, t, start);
		break;
	case BC_TAG:
		s = reader_i32(reader);
		cnt = reader_i32(reader);
		if (cnt < 0)
			die_at(unit_name(u), start,
			       "TAG with negative field count %d", cnt);

		in->op = I_TAG;
		in->str = s;
		in->imm = LtagHash((char *)string_at(u, s, start));
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
	d->start = d->reader.pos;
	d->opcode = reader_u8(&d->reader);
	in->raw = (int32_t)d->start;
	in->unit_id = d->reader.unit_id;
	decode_instruction(d, in);
	in->next = (struct code_ref){ in->unit_id, (int32_t)d->reader.pos };
	in->handler = execute_handler(in->op);
}

static void decoder_init(struct decoder *d, struct unit *u, size_t pos)
{
	memset(d, 0, sizeof(*d));
	reader_init(&d->reader, u->id, unit_code(u), u->code_size, 0);
	reader_seek(&d->reader, pos);
	d->cur_nargs = INVALID_INDEX;
	d->cur_nlocals = INVALID_INDEX;
	d->cur_ncaps = INVALID_INDEX;
}

void decode_insn(struct unit *u, int32_t offset, struct insn *in)
{
	struct decoder d;

	if (offset < 0 || (size_t)offset >= u->code_size)
		die("%s: instruction pointer is outside the code section",
		    unit_name(u));
	decoder_init(&d, u, (size_t)offset);
	decode_reader(&d, in);
}

void decode_capture(const struct insn *in, int32_t index, struct capture *cap)
{
	struct unit *u = unit_get(in->unit_id);
	struct byte_reader reader;

	reader_init(&reader, in->unit_id, unit_code(u), u->code_size, 0);
	reader_seek(&reader, in->caps + (size_t)index * (1 + sizeof(int32_t)));
	memset(cap, 0, sizeof(*cap));
	cap->kind = reader_u8(&reader);
	cap->index = reader_i32(&reader);
	if (cap->kind == K_GLO && cap->index < 0)
		(void)external_string_at(u, cap->index, in->raw);
}

static int is_boundary(const int32_t *owners, int32_t offset, size_t code_size)
{
	return offset >= 0 && (size_t)offset < code_size - 1 &&
	       owners[offset] != INVALID_BOUNDARY;
}

static void resolve_target(struct unit *u, const int32_t *owners,
			   struct insn *in)
{
	struct insn target;

	if (!is_boundary(owners, in->a, u->code_size))
		die_at(unit_name(u), in->raw,
		       "control-flow target %d is not an instruction boundary",
		       in->a);

	decode_insn(u, in->a, &target);
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
	} else if (owners[in->raw] != owners[in->a]) {
		die_at(unit_name(u), in->raw,
		       "jump target %d is outside the enclosing function body",
		       in->a);
	}
}

static int has_internal_target(const struct insn *in)
{
	if (in->op == I_CALL || in->op == I_CLOSURE)
		return in->a >= 0;
	return in->op == I_JMP || in->op == I_CJMPZ || in->op == I_CJMPNZ;
}

void unit_validate(struct unit *u)
{
	struct meta_scope scope = meta_scope_begin();
	size_t id = u->id, owners_handle;
	struct decoder d;
	struct insn in = { 0 };
	struct pubsym *p;
	int32_t *owners;
	int32_t current_fn = INVALID_INDEX;
	size_t pos;
	size_t i;

	if (u->code_size > SIZE_MAX / sizeof(*owners))
		die("code section is too large in unit %s", unit_name(u));
	owners_handle = meta_alloc_bytes(u->code_size * sizeof(*owners));
	u = unit_get(id);
	owners = meta_get(owners_handle);
	for (i = 0; i < u->code_size; i++)
		owners[i] = INVALID_BOUNDARY;

	decoder_init(&d, u, 0);
	while (d.reader.pos < u->code_size) {
		decode_reader(&d, &in);
		if (in.op == I_BEGIN || in.op == I_BEGINC)
			current_fn = in.raw;
		owners[in.raw] = current_fn;
	}
	if (in.op != I_EOF)
		die("%s: code section does not terminate with EOF",
		    unit_name(u));

	for (pos = 0; pos < u->code_size; pos = (size_t)in.next.offset) {
		decode_insn(u, (int32_t)pos, &in);
		if (has_internal_target(&in))
			resolve_target(u, owners, &in);
	}

	for (i = 0; i < u->pubs_count; i++) {
		p = unit_pub_at(u, i);
		if (p->flag != PUB_FUNCTION)
			continue;
		if (!is_boundary(owners, p->offset, u->code_size))
			die("%s: function \"%s\" offset %d is not a boundary",
			    unit_name(u), string_at(u, p->name, 0), p->offset);
		decode_insn(u, p->offset, &in);
		if (in.op != I_BEGIN && in.op != I_BEGINC)
			die("%s: function \"%s\" offset %d is not BEGIN",
			    unit_name(u), string_at(u, p->name, 0), p->offset);
		p->entry = p->offset;
	}
	u->validated = 1;
	meta_scope_end(scope);
}

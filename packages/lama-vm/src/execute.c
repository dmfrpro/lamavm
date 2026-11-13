#include "execute.h"
#include "builtins.h"
#include "decode.h"
#include "execute_internal.h"
#include "link.h"
#include "unit.h"
#include "util.h"

#include <stdint.h>

aint vm_stack_mem[STACK_WORDS];

enum {
	CLOSURE_ENTRY_INDEX = 0,
	SEXP_TAG_SLOT_COUNT = 1,
	STA_VALUE_INDEX = 0,
	STA_INDEX_INDEX = 1,
	STA_OBJECT_INDEX = 2,
};

static const opcode_handler handlers[UINT8_MAX + 1];

aint *execute_stack_init(size_t total_globals)
{
	aint *globals;

	if (total_globals > STACK_WORDS - STACK_RESERVE_WORDS)
		die("too many global variables: %zu", total_globals);

	globals = vm_stack_mem + STACK_WORDS - total_globals;
	__gc_stack_bottom = (size_t)(vm_stack_mem + STACK_WORDS);
	vm_set_sp(globals);
	return globals;
}

static void call_builtin(struct vm *vm, const struct insn *in)
{
	aint *args;
	aint result;

	args = vm_reversed_args(vm, in, in->b);
	result = in->bi->fn(args, in->b);
	vm_release_args(args, in->b);
	vm_push(in, result);
}

static void require_unboxed(const struct insn *in, aint x, aint y)
{
	if (!UNBOXED(x) || !UNBOXED(y))
		die_at(insn_unit_name(in), in->raw,
		       "binary operator 0x%02X expects unboxed operands",
		       in->kind);
}

static inline aint *binop_args(struct vm *vm, struct insn *in, aint *x, aint *y)
{
	aint *args;

	args = vm_args(vm, in, 2);
	*y = args[0];
	*x = args[1];
	return args;
}

static inline aint *unboxed_binop_args(struct vm *vm, struct insn *in, aint *x,
				       aint *y)
{
	aint *args;

	args = binop_args(vm, in, x, y);
	require_unboxed(in, *x, *y);
	return args;
}

static inline aint *divmod_args(struct vm *vm, struct insn *in, aint *x,
				aint *y)
{
	aint *args;

	args = unboxed_binop_args(vm, in, x, y);
	if (UNBOX(*y) == 0)
		die_at(insn_unit_name(in), in->raw, "%s by zero",
		       in->kind == BC_BINOP_DIV ? "division" : "modulo");
	if (UNBOX(*x) == INT64_MIN && UNBOX(*y) == -1)
		die_at(insn_unit_name(in), in->raw, "integer overflow in %s",
		       in->kind == BC_BINOP_DIV ? "division" : "modulo");
	return args;
}

static inline const uint8_t *binop_result(struct vm *vm, struct insn *in,
					  aint *args, aint result)
{
	vm_replace_args(args, 2, result);
	return in->next;
}

static const uint8_t *op_add(struct vm *vm, struct insn *in)
{
	aint *args;
	aint x, y;

	args = unboxed_binop_args(vm, in, &x, &y);
	return binop_result(vm, in, args, BOX(UNBOX(x) + UNBOX(y)));
}

static const uint8_t *op_sub(struct vm *vm, struct insn *in)
{
	aint *args;
	aint x, y, result;

	args = binop_args(vm, in, &x, &y);
	if (UNBOXED(x) || UNBOXED(y))
		require_unboxed(in, x, y);

	if (UNBOXED(x))
		result = BOX(UNBOX(x) - UNBOX(y));
	else
		result = BOX(x - y);
	return binop_result(vm, in, args, result);
}

static const uint8_t *op_mul(struct vm *vm, struct insn *in)
{
	aint *args;
	aint x, y;

	args = unboxed_binop_args(vm, in, &x, &y);
	return binop_result(vm, in, args, BOX(UNBOX(x) * UNBOX(y)));
}

static const uint8_t *op_div(struct vm *vm, struct insn *in)
{
	aint *args;
	aint x, y;

	args = divmod_args(vm, in, &x, &y);
	return binop_result(vm, in, args, BOX(UNBOX(x) / UNBOX(y)));
}

static const uint8_t *op_mod(struct vm *vm, struct insn *in)
{
	aint *args;
	aint x, y;

	args = divmod_args(vm, in, &x, &y);
	return binop_result(vm, in, args, BOX(UNBOX(x) % UNBOX(y)));
}

static const uint8_t *op_lt(struct vm *vm, struct insn *in)
{
	aint *args;
	aint x, y;

	args = unboxed_binop_args(vm, in, &x, &y);
	return binop_result(vm, in, args, BOX(UNBOX(x) < UNBOX(y)));
}

static const uint8_t *op_le(struct vm *vm, struct insn *in)
{
	aint *args;
	aint x, y;

	args = unboxed_binop_args(vm, in, &x, &y);
	return binop_result(vm, in, args, BOX(UNBOX(x) <= UNBOX(y)));
}

static const uint8_t *op_gt(struct vm *vm, struct insn *in)
{
	aint *args;
	aint x, y;

	args = unboxed_binop_args(vm, in, &x, &y);
	return binop_result(vm, in, args, BOX(UNBOX(x) > UNBOX(y)));
}

static const uint8_t *op_ge(struct vm *vm, struct insn *in)
{
	aint *args;
	aint x, y;

	args = unboxed_binop_args(vm, in, &x, &y);
	return binop_result(vm, in, args, BOX(UNBOX(x) >= UNBOX(y)));
}

static const uint8_t *op_eq(struct vm *vm, struct insn *in)
{
	aint *args;
	aint x, y;

	args = binop_args(vm, in, &x, &y);
	return binop_result(vm, in, args, BOX(x == y));
}

static const uint8_t *op_ne(struct vm *vm, struct insn *in)
{
	aint *args;
	aint x, y;

	args = unboxed_binop_args(vm, in, &x, &y);
	return binop_result(vm, in, args, BOX(UNBOX(x) != UNBOX(y)));
}

static const uint8_t *op_and(struct vm *vm, struct insn *in)
{
	aint *args;
	aint x, y;

	args = unboxed_binop_args(vm, in, &x, &y);
	return binop_result(vm, in, args, BOX(UNBOX(x) && UNBOX(y)));
}

static const uint8_t *op_or(struct vm *vm, struct insn *in)
{
	aint *args;
	aint x, y;

	args = unboxed_binop_args(vm, in, &x, &y);
	return binop_result(vm, in, args, BOX(UNBOX(x) || UNBOX(y)));
}

static const uint8_t *op_const(struct vm *vm, struct insn *in)
{
	vm_push(in, in->imm);
	return in->next;
}

static const uint8_t *op_string(struct vm *vm, struct insn *in)
{
	aint *args;
	void *object;

	vm_push(in, (aint)in->str);
	args = vm_args(vm, in, 1);
	object = Bstring(args);
	vm_release_args(args, 1);
	vm_push(in, (aint)object);
	return in->next;
}

static const uint8_t *op_sexp(struct vm *vm, struct insn *in)
{
	aint *args;
	void *object;
	int32_t n;

	n = in->a;
	vm_require(vm, in, n);
	vm_push(in, in->imm);
	args = vm_reversed_args(vm, in, n + SEXP_TAG_SLOT_COUNT);
	object = Bsexp(args, BOX(n + SEXP_TAG_SLOT_COUNT));
	vm_release_args(args, n + SEXP_TAG_SLOT_COUNT);
	vm_push(in, (aint)object);
	return in->next;
}

static const uint8_t *op_barray(struct vm *vm, struct insn *in)
{
	aint *args;
	void *object;
	int32_t n;

	n = in->a;
	args = vm_reversed_args(vm, in, n);
	object = Barray(args, BOX(n));
	vm_release_args(args, n);
	vm_push(in, (aint)object);
	return in->next;
}

static const uint8_t *op_sti(struct vm *vm, struct insn *in)
{
	aint *ref;
	aint v;

	v = vm_pop(vm, in);
	ref = (aint *)vm_pop(vm, in);
	*ref = v;
	vm_push(in, v);
	return in->next;
}

static const uint8_t *op_sta(struct vm *vm, struct insn *in)
{
	aint *args;
	aint v;

	args = vm_args(vm, in, STA_OBJECT_INDEX + 1);
	Bsta((void *)args[STA_OBJECT_INDEX], args[STA_INDEX_INDEX],
	     (void *)args[STA_VALUE_INDEX]);
	v = args[STA_VALUE_INDEX];
	vm_replace_args(args, STA_OBJECT_INDEX + 1, v);
	return in->next;
}

static const uint8_t *op_jmp(struct vm *vm, struct insn *in)
{
	return in->target;
}

static const uint8_t *op_cjmpz(struct vm *vm, struct insn *in)
{
	const uint8_t *next;
	aint v;

	next = in->next;
	v = vm_pop(vm, in);
	if (UNBOX(v) == 0)
		next = in->target;

	return next;
}

static const uint8_t *op_cjmpnz(struct vm *vm, struct insn *in)
{
	const uint8_t *next;
	aint v;

	next = in->next;
	v = vm_pop(vm, in);
	if (UNBOX(v) != 0)
		next = in->target;

	return next;
}

static const uint8_t *op_end(struct vm *vm, struct insn *in)
{
	const uint8_t *next;
	aint result;

	result = vm_pop(vm, in);
	if (vm_sp() != vm->operand_base)
		die_at(insn_unit_name(in), in->raw,
		       "END requires exactly one operand, have %zu",
		       (size_t)(vm->operand_base - vm_sp()) + 1);
	if (!vm->bp[FRAME_SAVED_IP])
		return NULL;

	next = frame_restore(vm);
	vm_push(in, result);
	return next;
}

static const uint8_t *op_ret(struct vm *vm, struct insn *in)
{
	const uint8_t *next;

	if (!vm->bp[FRAME_SAVED_IP])
		return NULL;

	next = frame_restore(vm);
	return next;
}

static const uint8_t *op_drop(struct vm *vm, struct insn *in)
{
	(void)vm_pop(vm, in);
	return in->next;
}

static const uint8_t *op_dup(struct vm *vm, struct insn *in)
{
	aint *args;
	aint v;

	args = vm_args(vm, in, 1);
	v = *args;
	vm_push(in, v);
	return in->next;
}

static const uint8_t *op_swap(struct vm *vm, struct insn *in)
{
	aint *args;
	aint t;

	args = vm_args(vm, in, 2);
	t = args[0];
	args[0] = args[1];
	args[1] = t;
	return in->next;
}

static const uint8_t *op_elem(struct vm *vm, struct insn *in)
{
	aint *args;
	aint result;

	args = vm_args(vm, in, 2);
	result = (aint)Belem((void *)args[1], args[0]);
	vm_replace_args(args, 2, result);
	return in->next;
}

static const uint8_t *op_ld_glo(struct vm *vm, struct insn *in)
{
	vm_push(in, *in->glob);
	return in->next;
}

static const uint8_t *op_ld_loc(struct vm *vm, struct insn *in)
{
	vm_push(in, vm->bp[FRAME_LOCALS - in->a]);
	return in->next;
}

static const uint8_t *op_ld_arg(struct vm *vm, struct insn *in)
{
	vm_push(in, *arg_addr(vm, in->a));
	return in->next;
}

static const uint8_t *op_ld_clo(struct vm *vm, struct insn *in)
{
	vm_push(in, *closure_addr(vm, in->a));
	return in->next;
}

static const uint8_t *op_lda(struct vm *vm, struct insn *in)
{
	vm_push(in, (aint)insn_var_addr(vm, in));
	return in->next;
}

static const uint8_t *op_st_glo(struct vm *vm, struct insn *in)
{
	aint *args;

	args = vm_args(vm, in, 1);
	*in->glob = *args;
	return in->next;
}

static const uint8_t *op_st_loc(struct vm *vm, struct insn *in)
{
	aint *args;

	args = vm_args(vm, in, 1);
	vm->bp[FRAME_LOCALS - in->a] = *args;
	return in->next;
}

static const uint8_t *op_st_arg(struct vm *vm, struct insn *in)
{
	aint *args;

	args = vm_args(vm, in, 1);
	*arg_addr(vm, in->a) = *args;
	return in->next;
}

static const uint8_t *op_st_clo(struct vm *vm, struct insn *in)
{
	aint *args;

	args = vm_args(vm, in, 1);
	*closure_addr(vm, in->a) = *args;
	return in->next;
}

static const uint8_t *op_begin(struct vm *vm, struct insn *in)
{
	if (UNBOX(vm->bp[FRAME_NARGS]) != in->a)
		die_at(insn_unit_name(in), in->raw,
		       "arity mismatch: function expects %d arguments, got %ld",
		       in->a, (long)UNBOX(vm->bp[FRAME_NARGS]));

	vm_init_locals(vm, in, in->b);
	return in->next;
}

static const uint8_t *op_call(struct vm *vm, struct insn *in)
{
	const uint8_t *next;
	aint *args;

	if (in->bi) {
		call_builtin(vm, in);
		next = in->next;
	} else {
		args = vm_args(vm, in, in->b);
		frame_push(vm, in, in->next, args + in->b, in->b);
		next = in->target;
	}

	return next;
}

static const uint8_t *op_callc(struct vm *vm, struct insn *in)
{
	const uint8_t *target;
	aint *args;
	aint c;

	args = vm_args(vm, in, in->a + CLOSURE_CAPTURE_OFFSET);
	c = args[in->a];
	if (UNBOXED(c) || TAG(TO_DATA(c)->data_header) != CLOSURE_TAG)
		die_at(insn_unit_name(in), in->raw,
		       "CALLC applied to a non-closure value");

	target = ((const uint8_t **)c)[CLOSURE_ENTRY_INDEX];
	frame_push(vm, in, in->next, args + in->a + CLOSURE_CAPTURE_OFFSET,
		   in->a);
	return target;
}

static const uint8_t *op_closure(struct vm *vm, struct insn *in)
{
	struct capture cap;
	aint *args;
	void *object;
	int32_t n, i;

	n = in->b;
	for (i = n - 1; i >= 0; i--) {
		decode_capture(in, i, &cap);
		link_capture(in, &cap);
		vm_push(in, load_capture(vm, &cap));
	}
	vm_push(in, (aint)in->target);

	args = vm_args(vm, in, n + CLOSURE_CAPTURE_OFFSET);
	object = Bclosure(args, BOX(n));
	vm_release_args(args, n + CLOSURE_CAPTURE_OFFSET);
	vm_push(in, (aint)object);
	return in->next;
}

static const uint8_t *op_tag(struct vm *vm, struct insn *in)
{
	aint *args;

	args = vm_args(vm, in, 1);
	*args = Btag((void *)*args, in->imm, BOX(in->a));
	return in->next;
}

static const uint8_t *op_array(struct vm *vm, struct insn *in)
{
	aint *args;

	args = vm_args(vm, in, 1);
	*args = Barray_patt((void *)*args, BOX(in->a));
	return in->next;
}

static const uint8_t *op_fail(struct vm *vm, struct insn *in)
{
	aint *args;

	args = vm_args(vm, in, 1);
	Bmatch_failure((void *)*args, (char *)insn_unit_name(in), BOX(in->a),
		       BOX(in->b));
	return in->next;
}

static const uint8_t *op_line(struct vm *vm, struct insn *in)
{
	return in->next;
}

static const uint8_t *op_patt(struct vm *vm, struct insn *in)
{
	aint *args;
	aint result;

	args = vm_args(vm, in, in->kind == PATT_STRING ? 2 : 1);
	switch (in->kind) {
	case PATT_STRING:
		result = Bstring_patt((void *)args[1], (void *)args[0]);
		vm_replace_args(args, 2, result);
		break;
	case PATT_STRING_TAG:
		*args = Bstring_tag_patt((void *)*args);
		break;
	case PATT_ARRAY_TAG:
		*args = Barray_tag_patt((void *)*args);
		break;
	case PATT_SEXP_TAG:
		*args = Bsexp_tag_patt((void *)*args);
		break;
	case PATT_BOXED:
		*args = Bboxed_patt((void *)*args);
		break;
	case PATT_UNBOXED:
		*args = Bunboxed_patt((void *)*args);
		break;
	case PATT_CLOSURE:
	default:
		*args = Bclosure_tag_patt((void *)*args);
		break;
	}

	return in->next;
}

static const uint8_t *op_eof(struct vm *vm, struct insn *in)
{
	(void)vm;
	(void)in;
	return NULL;
}

static const uint8_t *op_invalid(struct vm *vm, struct insn *in)
{
	(void)vm;
	die_at(insn_unit_name(in), in->raw,
	       "internal error: undecoded opcode %d", in->op);
}

static const opcode_handler handlers[UINT8_MAX + 1] = {
	[I_BINOP_ADD] = op_add,
	[I_BINOP_SUB] = op_sub,
	[I_BINOP_MUL] = op_mul,
	[I_BINOP_DIV] = op_div,
	[I_BINOP_MOD] = op_mod,
	[I_BINOP_LT] = op_lt,
	[I_BINOP_LE] = op_le,
	[I_BINOP_GT] = op_gt,
	[I_BINOP_GE] = op_ge,
	[I_BINOP_EQ] = op_eq,
	[I_BINOP_NE] = op_ne,
	[I_BINOP_AND] = op_and,
	[I_BINOP_OR] = op_or,
	[I_CONST] = op_const,
	[I_STRING] = op_string,
	[I_SEXP] = op_sexp,
	[I_STI] = op_sti,
	[I_STA] = op_sta,
	[I_JMP] = op_jmp,
	[I_END] = op_end,
	[I_RET] = op_ret,
	[I_DROP] = op_drop,
	[I_DUP] = op_dup,
	[I_SWAP] = op_swap,
	[I_ELEM] = op_elem,
	[I_LD_GLO] = op_ld_glo,
	[I_LD_LOC] = op_ld_loc,
	[I_LD_ARG] = op_ld_arg,
	[I_LD_CLO] = op_ld_clo,
	[I_LDA] = op_lda,
	[I_ST_GLO] = op_st_glo,
	[I_ST_LOC] = op_st_loc,
	[I_ST_ARG] = op_st_arg,
	[I_ST_CLO] = op_st_clo,
	[I_CJMPZ] = op_cjmpz,
	[I_CJMPNZ] = op_cjmpnz,
	[I_BEGIN] = op_begin,
	[I_BEGINC] = op_begin,
	[I_CLOSURE] = op_closure,
	[I_CALLC] = op_callc,
	[I_CALL] = op_call,
	[I_TAG] = op_tag,
	[I_ARRAY] = op_array,
	[I_FAIL] = op_fail,
	[I_LINE] = op_line,
	[I_PATT] = op_patt,
	[I_BARRAY] = op_barray,
	[I_EOF] = op_eof,
	[I_EOF + 1 ... UINT8_MAX] = op_invalid,
};

opcode_handler execute_handler(uint8_t op)
{
	return handlers[op];
}

void run(struct unit *entry)
{
	struct vm vm = { 0 };
	struct insn in;
	const uint8_t *code = entry->code;

	decode_insn(entry, code, &in);
	frame_push(&vm, &in, NULL, vm_sp(), in.a);
	while (code) {
		link_insn(&in);
		code = in.handler(&vm, &in);
		if (code)
			decode_insn(unit_of_code(code), code, &in);
	}
}

#include "execute.h"
#include "builtins.h"
#include "util.h"

#include <stdint.h>
#include <stdlib.h>

extern aint Ls__Infix_43(void *p, void *q);
extern aint Ls__Infix_45(void *p, void *q);
extern aint Ls__Infix_42(void *p, void *q);
extern aint Ls__Infix_47(void *p, void *q);
extern aint Ls__Infix_37(void *p, void *q);
extern aint Ls__Infix_60(void *p, void *q);
extern aint Ls__Infix_6061(void *p, void *q);
extern aint Ls__Infix_62(void *p, void *q);
extern aint Ls__Infix_6261(void *p, void *q);
extern aint Ls__Infix_6161(void *p, void *q);
extern aint Ls__Infix_3361(void *p, void *q);
extern aint Ls__Infix_3333(void *p, void *q);
extern aint Ls__Infix_3838(void *p, void *q);

static aint *stack_mem;
static aint *stack_limit;
static aint *stack_bottom;

aint *execute_stack_init(size_t total_globals)
{
	stack_mem = malloc(STACK_WORDS * sizeof(aint));
	if (!stack_mem)
		die("cannot allocate the operand stack");

	stack_bottom = stack_mem + STACK_WORDS;
	stack_limit = stack_mem + STACK_RESERVE_WORDS;
	__gc_stack_bottom = (size_t)stack_bottom;
	return stack_bottom - total_globals;
}

struct frame {
	struct insn *ip;
	aint *sp, *bp;
	void *closure;
	int32_t nargs;
};

struct vm {
	aint *sp;
	aint *bp;
	void *closure;
	int32_t nargs;
	int32_t call_nargs;
	struct frame *frames;
	size_t frames_count;
	size_t frames_cap;
};

static aint (*const binops[])(void *, void *) = {
	[BC_BINOP_ADD] = Ls__Infix_43,	[BC_BINOP_SUB] = Ls__Infix_45,
	[BC_BINOP_MUL] = Ls__Infix_42,	[BC_BINOP_DIV] = Ls__Infix_47,
	[BC_BINOP_MOD] = Ls__Infix_37,	[BC_BINOP_LT] = Ls__Infix_60,
	[BC_BINOP_LE] = Ls__Infix_6061, [BC_BINOP_GT] = Ls__Infix_62,
	[BC_BINOP_GE] = Ls__Infix_6261, [BC_BINOP_EQ] = Ls__Infix_6161,
	[BC_BINOP_NE] = Ls__Infix_3361, [BC_BINOP_AND] = Ls__Infix_3838,
	[BC_BINOP_OR] = Ls__Infix_3333,
};

enum {
	UNKNOWN_ARITY = -1,
	CLOSURE_ENTRY_INDEX = 0,
	CLOSURE_CAPTURE_OFFSET = 1,
	SEXP_TAG_SLOT_COUNT = 1,
	STA_VALUE_INDEX = 0,
	STA_INDEX_INDEX = 1,
	STA_OBJECT_INDEX = 2,
	STA_CONSUMED_SLOTS = 2,
};

typedef void (*opcode_handler)(struct vm *vm, struct insn *in);

static const opcode_handler dispatch[UINT8_MAX + 1];

static inline void dispatch_next(struct vm *vm, struct insn *in)
{
	dispatch[in->op](vm, in);
}

static inline void vm_push(struct vm *vm, aint value)
{
	if (vm->sp <= stack_limit)
		die("operand stack overflow");

	*--vm->sp = value;
}

static inline aint vm_pop(struct vm *vm)
{
	return *vm->sp++;
}

static void frame_push(struct vm *vm, struct insn *ip, aint *sp)
{
	struct frame *frame;

	if (vm->frames_count == vm->frames_cap) {
		if (vm->frames_cap)
			vm->frames_cap *= ARRAY_GROWTH_FACTOR;
		else
			vm->frames_cap = INITIAL_CALL_STACK_CAPACITY;
		vm->frames = xrealloc(vm->frames,
				      vm->frames_cap * sizeof(*vm->frames));
	}

	frame = &vm->frames[vm->frames_count++];
	frame->ip = ip;
	frame->sp = sp;
	frame->bp = vm->bp;
	frame->closure = vm->closure;
	frame->nargs = vm->nargs;
}

static struct insn *frame_restore(struct vm *vm)
{
	struct frame *frame;

	frame = &vm->frames[--vm->frames_count];
	vm->sp = frame->sp;
	vm->bp = frame->bp;
	vm->closure = frame->closure;
	vm->nargs = frame->nargs;
	return frame->ip;
}

static aint *copy_args(const aint *sp, int32_t n, size_t extra)
{
	aint *args = xmalloc(((size_t)n + extra) * sizeof(*args));
	int32_t i;

	for (i = 0; i < n; i++)
		args[i] = sp[n - 1 - i];

	return args;
}

static inline aint *var_addr(uint8_t kind, int32_t index, aint *glob, aint *bp,
			     void *closure, int32_t nargs)
{
	switch (kind) {
	case K_GLO:
		return glob;
	case K_LOC:
		return bp - 1 - index;
	case K_ARG:
		return bp + (nargs - 1 - index);
	default:
		return (aint *)closure + index + CLOSURE_CAPTURE_OFFSET;
	}
}

static inline aint load_var(const struct insn *in, aint *bp, void *closure,
			    int32_t nargs)
{
	return *var_addr(in->kind, in->a, in->glob, bp, closure, nargs);
}

static inline aint *insn_var_addr(const struct insn *in, aint *bp,
				  void *closure, int32_t nargs)
{
	return var_addr(in->kind, in->a, in->glob, bp, closure, nargs);
}

static inline aint load_capture(const struct capture *cap, aint *bp,
				void *closure, int32_t nargs)
{
	return *var_addr(cap->kind, cap->index, cap->glob, bp, closure, nargs);
}

static void call_builtin(struct vm *vm, const struct insn *in)
{
	aint *args;
	aint result;

	args = copy_args(vm->sp, in->b, 0);
	sync_gc(vm->sp);
	result = in->bi->fn(args, in->b);
	free(args);
	vm->sp += in->b;
	vm_push(vm, result);
}

static void op_binop(struct vm *vm, struct insn *in)
{
	aint x, y;

	y = vm_pop(vm);
	x = vm_pop(vm);
	vm_push(vm, binops[in->kind]((void *)x, (void *)y));
	dispatch_next(vm, in + 1);
}

static void op_const(struct vm *vm, struct insn *in)
{
	vm_push(vm, in->imm);
	dispatch_next(vm, in + 1);
}

static void *make_string(struct vm *vm, const struct insn *in)
{
	aint string_arg[1];

	string_arg[0] = (aint)in->str;
	sync_gc(vm->sp);
	return Bstring(string_arg);
}

static void op_string(struct vm *vm, struct insn *in)
{
	void *object;

	object = make_string(vm, in);
	vm_push(vm, (aint)object);
	dispatch_next(vm, in + 1);
}

static void op_sexp(struct vm *vm, struct insn *in)
{
	aint *args;
	void *object;
	int32_t n;

	n = in->a;
	args = copy_args(vm->sp, n, SEXP_TAG_SLOT_COUNT);
	args[n] = in->imm;
	sync_gc(vm->sp);
	object = Bsexp(args, BOX(n + SEXP_TAG_SLOT_COUNT));
	free(args);
	vm->sp += n;
	vm_push(vm, (aint)object);
	dispatch_next(vm, in + 1);
}

static void op_barray(struct vm *vm, struct insn *in)
{
	aint *args;
	void *object;
	int32_t n;

	n = in->a;
	args = copy_args(vm->sp, n, 0);
	sync_gc(vm->sp);
	object = Barray(args, BOX(n));
	free(args);
	vm->sp += n;
	vm_push(vm, (aint)object);
	dispatch_next(vm, in + 1);
}

static void op_sti(struct vm *vm, struct insn *in)
{
	aint *ref;
	aint v;

	v = vm_pop(vm);
	ref = (aint *)vm_pop(vm);
	*ref = v;
	vm_push(vm, v);
	dispatch_next(vm, in + 1);
}

static void op_sta(struct vm *vm, struct insn *in)
{
	aint v, i, x;

	v = vm->sp[STA_VALUE_INDEX];
	i = vm->sp[STA_INDEX_INDEX];
	x = vm->sp[STA_OBJECT_INDEX];
	sync_gc(vm->sp);
	Bsta((void *)x, i, (void *)v);
	vm->sp += STA_CONSUMED_SLOTS;
	*vm->sp = v;
	dispatch_next(vm, in + 1);
}

static void op_jmp(struct vm *vm, struct insn *in)
{
	dispatch_next(vm, in->target);
}

static void op_cjmp(struct vm *vm, struct insn *in)
{
	struct insn *next;
	aint v;
	int zero;

	next = in + 1;
	v = vm_pop(vm);
	zero = UNBOX(v) == 0;
	if ((in->op == I_CJMPZ && zero) || (in->op == I_CJMPNZ && !zero))
		next = in->target;

	dispatch_next(vm, next);
}

static void op_end(struct vm *vm, struct insn *in)
{
	struct insn *next;
	aint result;

	result = vm_pop(vm);
	if (vm->frames_count == 0)
		return;

	next = frame_restore(vm);
	vm_push(vm, result);
	dispatch_next(vm, next);
}

static void op_ret(struct vm *vm, struct insn *in)
{
	struct insn *next;

	if (vm->frames_count == 0)
		return;

	next = frame_restore(vm);
	dispatch_next(vm, next);
}

static void op_drop(struct vm *vm, struct insn *in)
{
	vm->sp++;
	dispatch_next(vm, in + 1);
}

static void op_dup(struct vm *vm, struct insn *in)
{
	aint v;

	v = *vm->sp;
	vm_push(vm, v);
	dispatch_next(vm, in + 1);
}

static void op_swap(struct vm *vm, struct insn *in)
{
	aint t;

	t = vm->sp[0];
	vm->sp[0] = vm->sp[1];
	vm->sp[1] = t;
	dispatch_next(vm, in + 1);
}

static void op_elem(struct vm *vm, struct insn *in)
{
	aint i, p, r;

	i = vm->sp[0];
	p = vm->sp[1];
	sync_gc(vm->sp);
	r = (aint)Belem((void *)p, i);
	vm->sp++;
	*vm->sp = r;
	dispatch_next(vm, in + 1);
}

static void op_ld(struct vm *vm, struct insn *in)
{
	vm_push(vm, load_var(in, vm->bp, vm->closure, vm->nargs));
	dispatch_next(vm, in + 1);
}

static void op_lda(struct vm *vm, struct insn *in)
{
	vm_push(vm, (aint)insn_var_addr(in, vm->bp, vm->closure, vm->nargs));
	dispatch_next(vm, in + 1);
}

static void op_st(struct vm *vm, struct insn *in)
{
	*insn_var_addr(in, vm->bp, vm->closure, vm->nargs) = *vm->sp;
	dispatch_next(vm, in + 1);
}

static void op_begin(struct vm *vm, struct insn *in)
{
	int32_t n;

	if (vm->call_nargs >= 0 && vm->call_nargs != in->a)
		die_at(in->unit->name, in->raw,
		       "arity mismatch: function expects %d arguments, got %d",
		       in->a, vm->call_nargs);

	vm->nargs = in->a;
	for (n = 0; n < in->b; n++)
		vm_push(vm, BOX(0));

	dispatch_next(vm, in + 1);
}

static void op_call(struct vm *vm, struct insn *in)
{
	struct insn *next;

	if (in->bi) {
		call_builtin(vm, in);
		next = in + 1;
	} else {
		frame_push(vm, in + 1, vm->sp + in->b);
		vm->bp = vm->sp;
		vm->call_nargs = in->b;
		next = in->target;
	}

	dispatch_next(vm, next);
}

static void op_callc(struct vm *vm, struct insn *in)
{
	struct insn *target;
	aint c;

	c = vm->sp[in->a];
	if (UNBOXED(c) || TAG(TO_DATA(c)->data_header) != CLOSURE_TAG)
		die_at(in->unit->name, in->raw,
		       "CALLC applied to a non-closure value");

	target = ((struct insn **)c)[CLOSURE_ENTRY_INDEX];
	frame_push(vm, in + 1, vm->sp + in->a + CLOSURE_CAPTURE_OFFSET);
	vm->closure = (void *)c;
	vm->bp = vm->sp;
	vm->call_nargs = in->a;
	dispatch_next(vm, target);
}

static void op_closure(struct vm *vm, struct insn *in)
{
	aint *tmp;
	struct capture *cap;
	void *object;
	size_t size;
	int32_t n, i;

	n = in->b;
	size = (size_t)n + CLOSURE_CAPTURE_OFFSET;
	tmp = xmalloc(size * sizeof(*tmp));
	tmp[CLOSURE_ENTRY_INDEX] = (aint)in->target;

	for (i = 0; i < n; i++) {
		cap = &in->caps[i];
		tmp[i + CLOSURE_CAPTURE_OFFSET] =
			load_capture(cap, vm->bp, vm->closure, vm->nargs);
	}

	sync_gc(vm->sp);
	object = Bclosure(tmp, BOX(n));
	free(tmp);
	vm_push(vm, (aint)object);
	dispatch_next(vm, in + 1);
}

static void op_tag(struct vm *vm, struct insn *in)
{
	*vm->sp = Btag((void *)*vm->sp, in->imm, BOX(in->a));
	dispatch_next(vm, in + 1);
}

static void op_array(struct vm *vm, struct insn *in)
{
	*vm->sp = Barray_patt((void *)*vm->sp, BOX(in->a));
	dispatch_next(vm, in + 1);
}

static void op_fail(struct vm *vm, struct insn *in)
{
	sync_gc(vm->sp);
	Bmatch_failure((void *)*vm->sp, (char *)in->name, BOX(in->a),
		       BOX(in->b));
	dispatch_next(vm, in + 1);
}

static void op_line(struct vm *vm, struct insn *in)
{
	dispatch_next(vm, in + 1);
}

static void op_patt(struct vm *vm, struct insn *in)
{
	aint x, y;

	switch (in->kind) {
	case PATT_STRING:
		y = vm_pop(vm);
		x = vm_pop(vm);
		vm_push(vm, Bstring_patt((void *)x, (void *)y));
		break;
	case PATT_STRING_TAG:
		*vm->sp = Bstring_tag_patt((void *)*vm->sp);
		break;
	case PATT_ARRAY_TAG:
		*vm->sp = Barray_tag_patt((void *)*vm->sp);
		break;
	case PATT_SEXP_TAG:
		*vm->sp = Bsexp_tag_patt((void *)*vm->sp);
		break;
	case PATT_BOXED:
		*vm->sp = Bboxed_patt((void *)*vm->sp);
		break;
	case PATT_UNBOXED:
		*vm->sp = Bunboxed_patt((void *)*vm->sp);
		break;
	case PATT_CLOSURE:
	default:
		*vm->sp = Bclosure_tag_patt((void *)*vm->sp);
		break;
	}

	dispatch_next(vm, in + 1);
}

static void op_eof(struct vm *vm, struct insn *in)
{
	(void)vm;
	(void)in;
}

static void op_invalid(struct vm *vm, struct insn *in)
{
	(void)vm;
	die_at(in->unit->name, in->raw, "internal error: undecoded opcode %d",
	       in->op);
}

static const opcode_handler dispatch[UINT8_MAX + 1] = {
	[I_BINOP] = op_binop,
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
	[I_LD] = op_ld,
	[I_LDA] = op_lda,
	[I_ST] = op_st,
	[I_CJMPZ] = op_cjmp,
	[I_CJMPNZ] = op_cjmp,
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

void run(struct insn *entry, aint *sp0)
{
	struct vm vm = {
		.sp = sp0,
		.bp = sp0,
		.call_nargs = UNKNOWN_ARITY,
	};

	dispatch_next(&vm, entry);
}

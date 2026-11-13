#ifndef LAMA_VM_EXECUTE_INTERNAL_H
#define LAMA_VM_EXECUTE_INTERNAL_H

#include "execute.h"
#include "unit.h"
#include "util.h"

#include <stdint.h>

extern aint vm_stack_mem[STACK_WORDS];

struct vm {
	aint *bp;
	aint *operand_base;
};

enum {
	FRAME_NARGS = 0,
	FRAME_SAVED_BP = -1,
	FRAME_SAVED_IP = -2,
	FRAME_SAVED_UNIT = -3,
	FRAME_SAVED_SP = -4,
	FRAME_SAVED_BASE = -5,
	FRAME_LOCALS = -6,
	CLOSURE_ARG_OFFSET = 1,
	CLOSURE_CAPTURE_OFFSET = 2,
};

static inline struct code_ref code_ref_none(void)
{
	return (struct code_ref){ .offset = -1 };
}

static inline aint *vm_sp(void)
{
	return (aint *)(__gc_stack_top + sizeof(size_t));
}

static inline void vm_set_sp(aint *sp)
{
	__gc_stack_top = (size_t)(sp - 1);
}

static inline const char *insn_unit_name(const struct insn *in)
{
	return unit_name(unit_get(in->unit_id));
}

static _Noreturn void vm_underflow(const struct insn *in, int32_t n,
				   size_t available)
{
	die_at(insn_unit_name(in), in->raw,
	       "operand stack underflow: need %d values, have %zu", n,
	       available);
}

static inline void vm_require(const struct vm *vm, const struct insn *in,
			      int32_t n)
{
	aint *sp;
	size_t available;

	sp = vm_sp();
	if (__builtin_expect(sp > vm->operand_base, 0))
		die_at(insn_unit_name(in), in->raw,
		       "internal error: invalid operand stack boundary");

	available = (size_t)(vm->operand_base - sp);
	if (__builtin_expect(available < (size_t)n, 0))
		vm_underflow(in, n, available);
}

static inline aint *vm_args(struct vm *vm, const struct insn *in, int32_t n)
{
	vm_require(vm, in, n);
	return vm_sp();
}

static inline aint *vm_reserve(const struct insn *in, int32_t n)
{
	aint *sp;

	sp = vm_sp();
	if ((size_t)(sp - vm_stack_mem - STACK_RESERVE_WORDS) < (size_t)n)
		die_at(insn_unit_name(in), in->raw, "operand stack overflow");

	sp -= n;
	vm_set_sp(sp);
	return sp;
}

static inline void vm_push(const struct insn *in, aint value)
{
	aint *sp;

	sp = vm_reserve(in, 1);
	*sp = value;
}

static inline aint vm_pop(struct vm *vm, const struct insn *in)
{
	aint *args;

	args = vm_args(vm, in, 1);
	vm_set_sp(args + 1);
	return *args;
}

static inline void vm_replace_args(aint *args, int32_t n, aint result)
{
	vm_set_sp(args + n - 1);
	*vm_sp() = result;
}

static inline void vm_release_args(aint *args, int32_t n)
{
	vm_set_sp(args + n);
}

static inline aint *vm_reversed_args(struct vm *vm, const struct insn *in,
				     int32_t n)
{
	aint *args;
	aint tmp;
	int32_t i;

	args = vm_args(vm, in, n);
	for (i = 0; i < n / 2; i++) {
		tmp = args[i];
		args[i] = args[n - 1 - i];
		args[n - 1 - i] = tmp;
	}
	return args;
}

static inline void vm_init_locals(struct vm *vm, const struct insn *in,
				  int32_t n)
{
	aint *sp;
	int32_t i;

	sp = vm_reserve(in, n);
	for (i = 0; i < n; i++)
		sp[i] = BOX(0);
	vm->operand_base = sp;
}

static inline void frame_push(struct vm *vm, const struct insn *in,
			      struct code_ref return_ip, aint *caller_sp,
			      int32_t nargs)
{
	aint *bp;
	aint *sp;

	sp = vm_reserve(in, -FRAME_LOCALS);
	bp = sp - FRAME_LOCALS - 1;
	bp[FRAME_NARGS] = BOX(nargs);
	bp[FRAME_SAVED_BP] = (aint)vm->bp;
	bp[FRAME_SAVED_IP] = BOX(return_ip.offset);
	bp[FRAME_SAVED_UNIT] = BOX(return_ip.unit_id);
	bp[FRAME_SAVED_SP] = (aint)caller_sp;
	bp[FRAME_SAVED_BASE] = (aint)vm->operand_base;
	vm->bp = bp;
	vm->operand_base = sp;
}

static struct code_ref frame_restore(struct vm *vm)
{
	struct code_ref next;

	next = (struct code_ref){ .unit_id = UNBOX(vm->bp[FRAME_SAVED_UNIT]),
				  .offset = UNBOX(vm->bp[FRAME_SAVED_IP]) };
	vm_set_sp((aint *)vm->bp[FRAME_SAVED_SP]);
	vm->operand_base = (aint *)vm->bp[FRAME_SAVED_BASE];
	vm->bp = (aint *)vm->bp[FRAME_SAVED_BP];
	return next;
}

static inline aint *arg_addr(const struct vm *vm, int32_t index)
{
	return vm->bp + UNBOX(vm->bp[FRAME_NARGS]) - index;
}

static inline aint *closure_addr(const struct vm *vm, int32_t index)
{
	int32_t nargs;
	void *closure;

	nargs = UNBOX(vm->bp[FRAME_NARGS]);
	closure = (void *)vm->bp[nargs + CLOSURE_ARG_OFFSET];
	return (aint *)closure + index + CLOSURE_CAPTURE_OFFSET;
}

static inline aint *var_addr(const struct vm *vm, uint8_t kind, int32_t index,
			     aint *glob)
{
	switch (kind) {
	case K_GLO:
		return glob;
	case K_LOC:
		return vm->bp + FRAME_LOCALS - index;
	case K_ARG:
		return arg_addr(vm, index);
	default:
		return closure_addr(vm, index);
	}
}

static inline aint *insn_var_addr(const struct vm *vm, const struct insn *in)
{
	return var_addr(vm, in->kind, in->a, in->glob);
}

static inline aint load_capture(const struct vm *vm, const struct capture *cap)
{
	return *var_addr(vm, cap->kind, cap->index, cap->glob);
}

#endif

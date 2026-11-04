#ifndef LAMA_VM_EXECUTE_H
#define LAMA_VM_EXECUTE_H

#include "vm.h"

#define STACK_WORDS (8ull * 1024 * 1024)
#define STACK_RESERVE_WORDS 16
#define INITIAL_CALL_STACK_CAPACITY 1024

aint *execute_stack_init(size_t total_globals);

static inline void sync_gc(aint *sp)
{
	__gc_stack_top = (size_t)sp - sizeof(size_t);
}

void run(struct insn *entry, aint *sp0);

#endif

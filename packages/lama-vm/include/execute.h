#ifndef LAMA_VM_EXECUTE_H
#define LAMA_VM_EXECUTE_H

#include "vm.h"

#define STACK_WORDS (8ull * 1024 * 1024)
#define STACK_RESERVE_WORDS 16

aint *execute_stack_init(size_t total_globals);
opcode_handler execute_handler(uint8_t op);
void run(struct insn *entry);

#endif

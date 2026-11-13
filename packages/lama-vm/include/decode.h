#ifndef LAMA_VM_DECODE_H
#define LAMA_VM_DECODE_H

#include "vm.h"

void unit_validate(struct unit *u);
void decode_insn(struct unit *u, int32_t offset, struct insn *in);
void decode_capture(const struct insn *in, int32_t index, struct capture *cap);

#endif

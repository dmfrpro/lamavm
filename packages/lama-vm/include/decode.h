#ifndef LAMA_VM_DECODE_H
#define LAMA_VM_DECODE_H

#include "vm.h"

void unit_validate(struct unit *u);
void decode_insn(struct unit *u, const uint8_t *code, struct insn *in);
void decode_capture(const struct insn *in, int32_t index, struct capture *cap);
const uint8_t *decode_find(struct unit *u, int32_t offset, struct insn *in);

#endif

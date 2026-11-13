#ifndef LAMA_VM_LINK_H
#define LAMA_VM_LINK_H

#include "vm.h"

void link_units(void);
void link_insn(struct insn *in);
void link_capture(const struct insn *in, struct capture *cap);

#endif

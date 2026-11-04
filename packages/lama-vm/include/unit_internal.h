#ifndef LAMA_VM_UNIT_INTERNAL_H
#define LAMA_VM_UNIT_INTERNAL_H

#include "vm.h"

int32_t rd_i32(const uint8_t *p);
const char *string_at(const struct unit *u, int32_t off, size_t where);
const char *external_string_at(const struct unit *u, int32_t ref, size_t where);

#endif

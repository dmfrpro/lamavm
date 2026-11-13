#ifndef LAMA_VM_UTIL_H
#define LAMA_VM_UTIL_H

#include "vm.h"

extern const char *const progname;

_Noreturn void die(const char *fmt, ...);
_Noreturn void die_at(const char *unit, size_t off, const char *fmt, ...);
const char *path_basename(const char *p);

#endif

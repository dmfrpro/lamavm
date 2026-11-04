#ifndef LAMA_VM_BUILTINS_H
#define LAMA_VM_BUILTINS_H

#include "vm.h"

struct builtin {
	const char *name;

	aint (*fn)(aint *, int);
};

const struct builtin *builtin_find(const char *name);

#endif

#ifndef LAMA_VM_META_H
#define LAMA_VM_META_H

#include "vm.h"

struct meta_scope {
	size_t count;
};

void meta_init(void);
size_t meta_alloc_bytes(size_t n);
size_t meta_alloc_array(size_t n);
size_t meta_alloc_string(const char *s);
void meta_replace_array(size_t handle, size_t n);
void *meta_get(size_t handle);
struct meta_scope meta_scope_begin(void);
void meta_scope_end(struct meta_scope scope);

#endif

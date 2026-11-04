#ifndef LAMA_VM_UNIT_H
#define LAMA_VM_UNIT_H

#include "vm.h"

#define MAX_SEARCH_PATHS 64

extern struct unit **units;
extern size_t units_count;
extern const char *search_paths[MAX_SEARCH_PATHS];
extern size_t search_paths_count;

struct unit *unit_load(const char *path, const char *name);
struct unit *unit_find_file(const char *name, const char *relative_to);

#endif

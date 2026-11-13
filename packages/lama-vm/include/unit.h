#ifndef LAMA_VM_UNIT_H
#define LAMA_VM_UNIT_H

#include "vm.h"

#define UNIT_NONE SIZE_MAX

size_t unit_count(void);
void unit_init(void);
void unit_add_search_path(const char *path);
const char *unit_search_path_at(size_t index);
struct unit *unit_get(size_t id);
struct unit *unit_at(size_t index);
const char *unit_name(const struct unit *u);
const char *unit_path(const struct unit *u);
const char *unit_import_at(const struct unit *u, size_t index);
struct pubsym *unit_pub_at(const struct unit *u, size_t index);
size_t unit_load(const char *path, const char *name);
size_t unit_find_file(const char *name, const char *relative_to);
struct unit *unit_of_code(const uint8_t *code);

#endif

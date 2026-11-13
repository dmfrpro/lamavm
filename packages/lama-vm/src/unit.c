#include "unit.h"

#include "format.h"
#include "unit_internal.h"
#include "util.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	INITIAL_ROOT_CAPACITY = 8,
	INITIAL_UNIT_CAPACITY = 8,
	ARRAY_GROWTH_FACTOR = 2
};

struct meta_array {
	size_t handle;
	size_t count;
	size_t capacity;
};

static aint *meta_roots;
static size_t meta_roots_count;
static size_t meta_roots_cap;
static struct meta_array units;
static struct meta_array search_paths;

static aint *array_contents(void *object)
{
	data *obj = object;

	if (!obj)
		die("out of memory allocating decoder metadata");
	return (aint *)obj->contents;
}

static void check_array_size(size_t n)
{
	if (n > SIZE_MAX / sizeof(aint))
		die("decoder metadata array is too large: %zu elements", n);
}

static aint *alloc_array_contents(size_t n)
{
	aint *contents;
	size_t i;

	contents = array_contents(alloc_array(n));
	for (i = 0; i < n; i++)
		contents[i] = BOX(0);
	return contents;
}

static void reserve_handle(void)
{
	aint *new_roots;
	size_t new_cap;

	if (meta_roots_count < meta_roots_cap)
		return;
	if (meta_roots_cap > SIZE_MAX / ARRAY_GROWTH_FACTOR)
		die("too many decoder metadata objects");

	new_cap = meta_roots_cap ? meta_roots_cap * ARRAY_GROWTH_FACTOR :
				   INITIAL_ROOT_CAPACITY;
	check_array_size(new_cap);
	new_roots = alloc_array_contents(new_cap);
	memcpy(new_roots, meta_roots, meta_roots_count * sizeof(*new_roots));
	meta_roots = new_roots;
	meta_roots_cap = new_cap;
}

static size_t store_object(void *object)
{
	size_t handle = meta_roots_count++;

	meta_roots[handle] = (aint)object;
	return handle;
}

static void meta_init(void)
{
	push_extra_root((void **)&meta_roots);
	meta_roots = alloc_array_contents(INITIAL_ROOT_CAPACITY);
	meta_roots_cap = INITIAL_ROOT_CAPACITY;
}

static size_t meta_alloc_bytes(size_t n)
{
	data *obj;

	reserve_handle();
	obj = alloc_string(n);
	if (!obj)
		die("out of memory allocating %zu bytes of decoder metadata",
		    n);
	return store_object(obj->contents);
}

static size_t meta_alloc_array(size_t n)
{
	check_array_size(n);
	reserve_handle();
	return store_object(alloc_array_contents(n));
}

static void *meta_get(size_t handle)
{
	if (handle >= meta_roots_count || meta_roots[handle] == BOX(0))
		die("invalid decoder metadata handle %zu", handle);
	return (void *)meta_roots[handle];
}

static size_t meta_alloc_string(const char *s)
{
	size_t handle;
	size_t n = strlen(s);

	handle = meta_alloc_bytes(n);
	memcpy(meta_get(handle), s, n + 1);
	return handle;
}

static void meta_replace_array(size_t handle, size_t n)
{
	aint *old;
	aint *new;
	size_t old_n;

	(void)meta_get(handle);
	check_array_size(n);
	new = alloc_array_contents(n);
	old = meta_get(handle);
	old_n = LEN(TO_DATA(old)->data_header);
	memcpy(new, old, (old_n < n ? old_n : n) * sizeof(*new));
	meta_roots[handle] = (aint)new;
}

static void meta_array_init(struct meta_array *array)
{
	array->handle = meta_alloc_array(INITIAL_UNIT_CAPACITY);
	array->count = 0;
	array->capacity = INITIAL_UNIT_CAPACITY;
}

static aint *meta_array_contents(const struct meta_array *array)
{
	return meta_get(array->handle);
}

static void meta_array_push(struct meta_array *array, aint value,
			    const char *description)
{
	aint *contents;

	if (array->count == array->capacity) {
		if (array->capacity > SIZE_MAX / ARRAY_GROWTH_FACTOR)
			die("too many %s", description);
		array->capacity *= ARRAY_GROWTH_FACTOR;
		meta_replace_array(array->handle, array->capacity);
	}
	contents = meta_array_contents(array);
	contents[array->count++] = value;
}

size_t unit_count(void)
{
	return units.count;
}

void unit_init(void)
{
	meta_init();
	meta_array_init(&units);
	meta_array_init(&search_paths);
}

struct unit *unit_get(size_t id)
{
	return meta_get(id);
}

struct unit *unit_at(size_t index)
{
	if (index >= units.count)
		die("invalid bytecode unit index %zu", index);
	return unit_get(UNBOX(meta_array_contents(&units)[index]));
}

void unit_add_search_path(const char *path)
{
	meta_array_push(&search_paths, (aint)path, "bytecode search paths");
}

const char *unit_search_path_at(size_t index)
{
	if (index >= search_paths.count)
		die("invalid bytecode search path index %zu", index);
	return (const char *)meta_array_contents(&search_paths)[index];
}

const char *unit_name(const struct unit *u)
{
	return meta_get(u->name);
}

const char *unit_path(const struct unit *u)
{
	return meta_get(u->path);
}

const char *unit_import_at(const struct unit *u, size_t index)
{
	const char **imports;

	if (index >= u->imports_count)
		die("invalid import index %zu in unit %s", index, unit_name(u));
	imports = meta_get(u->imports);
	return imports[index];
}

struct pubsym *unit_pub_at(const struct unit *u, size_t index)
{
	if (index >= u->pubs_count)
		die("invalid public symbol index %zu in unit %s", index,
		    unit_name(u));
	return (struct pubsym *)meta_get(u->pubs) + index;
}

int32_t rd_i32(const uint8_t *p)
{
	uint32_t v = 0;
	size_t i;

	for (i = 0; i < BC_I32_SIZE; i++)
		v |= (uint32_t)p[i] << (i * BYTE_WIDTH);

	return (int32_t)v;
}

static uint8_t *read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	uint8_t *buf;
	long n;

	if (!f)
		die("cannot open %s: %s", path, strerror(errno));
	if (fseek(f, 0, SEEK_END) != 0)
		die("cannot seek %s", path);

	n = ftell(f);
	if (n < 0)
		die("cannot determine size of %s", path);

	rewind(f);
	buf = xmalloc((size_t)n + 1);
	if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n)
		die("cannot read %s", path);

	fclose(f);
	buf[n] = 0;
	*size = (size_t)n;
	return buf;
}

const char *string_at(const struct unit *u, int32_t off, size_t where)
{
	const char *s;

	if (off < 0 || (size_t)off >= u->strings_size)
		die_at(unit_name(u), where,
		       "string table offset %d is out of range [0, %zu)", off,
		       u->strings_size);

	s = u->strings + off;
	if (!memchr(s, '\0', u->strings_size - (size_t)off))
		die_at(unit_name(u), where,
		       "string at offset %d is not NUL-terminated", off);

	return s;
}

const char *external_string_at(const struct unit *u, int32_t ref, size_t where)
{
	return string_at(u, -ref - EXTERNAL_REF_BIAS, where);
}

struct unit_layout {
	uint64_t strings_end;
	uint64_t imports_end;
	uint64_t pubs_end;
};

static size_t unit_find_cached(const char *name)
{
	struct unit *u;
	size_t i;

	for (i = 0; i < units.count; i++) {
		u = unit_at(i);
		if (strcmp(unit_name(u), name) == 0)
			return u->id;
	}

	return UNIT_NONE;
}

static size_t unit_create(const char *path, const char *name)
{
	struct unit *u;
	size_t id, name_handle, path_handle;

	name_handle = meta_alloc_string(name);
	path_handle = meta_alloc_string(path);
	id = meta_alloc_bytes(sizeof(*u));
	u = unit_get(id);
	memset(u, 0, sizeof(*u));
	u->id = id;
	u->name = name_handle;
	u->path = path_handle;
	u->image = read_file(path, &u->image_size);
	return id;
}

static struct unit_layout unit_parse_header(struct unit *u)
{
	struct unit_layout layout;
	uint64_t off;
	int32_t version, strings_size, globals_count, imports_count, pubs_count;

	if (u->image_size < BC_HEADER_SIZE)
		die("%s: short bytecode header (%zu bytes, need %d)",
		    unit_path(u), u->image_size, BC_HEADER_SIZE);

	if (memcmp(u->image, "LaMa", BC_MAGIC_SIZE) != 0)
		die("%s: bad magic, expected \"LaMa\"", unit_path(u));

	version = rd_i32(u->image + BC_VERSION_OFFSET);
	if (version != BC_FORMAT_VERSION)
		die("%s: unsupported bytecode version %d (expected %d)",
		    unit_path(u), version, BC_FORMAT_VERSION);

	strings_size = rd_i32(u->image + BC_STRINGS_SIZE_OFFSET);
	globals_count = rd_i32(u->image + BC_GLOBALS_COUNT_OFFSET);
	imports_count = rd_i32(u->image + BC_IMPORTS_COUNT_OFFSET);
	pubs_count = rd_i32(u->image + BC_PUBS_COUNT_OFFSET);

	if (strings_size < 0)
		die("%s: negative string table size %d", unit_path(u),
		    strings_size);
	if (globals_count < 0)
		die("%s: negative globals count %d", unit_path(u),
		    globals_count);
	if (imports_count < 0)
		die("%s: negative imports count %d", unit_path(u),
		    imports_count);
	if (pubs_count < 0)
		die("%s: negative public symbols count %d", unit_path(u),
		    pubs_count);

	off = BC_HEADER_SIZE;
	layout.strings_end = off + (uint64_t)strings_size;
	layout.imports_end = layout.strings_end +
			     (uint64_t)BC_IMPORT_SIZE * (uint64_t)imports_count;
	layout.pubs_end = layout.imports_end +
			  (uint64_t)BC_PUB_SIZE * (uint64_t)pubs_count;

	if (layout.pubs_end > u->image_size)
		die("%s: truncated file: need %llu bytes, have %zu",
		    unit_path(u), (unsigned long long)layout.pubs_end,
		    u->image_size);
	if (layout.pubs_end == u->image_size)
		die("%s: missing code section", unit_path(u));

	u->strings = (const char *)u->image + off;
	u->strings_size = (size_t)strings_size;
	u->globals_count = (size_t)globals_count;
	u->imports_count = (size_t)imports_count;
	u->pubs_count = (size_t)pubs_count;
	u->code = u->image + layout.pubs_end;
	u->code_size = u->image_size - layout.pubs_end;

	if (u->code[u->code_size - 1] != BC_EOF)
		die("%s: code section does not end with the EOF opcode (0xFF)",
		    unit_path(u));

	return layout;
}

static void unit_parse_imports(struct unit *u, uint64_t strings_end)
{
	const char **imports;
	size_t handle, id = u->id;
	size_t import_off;
	size_t i;
	int32_t so;

	if (u->imports_count > SIZE_MAX / sizeof(*imports))
		die("too many imports in unit %s", unit_name(u));
	handle = meta_alloc_bytes(u->imports_count * sizeof(*imports));
	u = unit_get(id);
	u->imports = handle;
	imports = meta_get(handle);
	for (i = 0; i < u->imports_count; i++) {
		import_off = strings_end + BC_IMPORT_SIZE * i;
		so = rd_i32(u->image + import_off);
		imports[i] = string_at(u, so, import_off);
	}
}

static void unit_parse_public(struct unit *u, size_t i, size_t pub_off)
{
	const uint8_t *p;
	struct pubsym *pub;
	size_t j;

	p = u->image + pub_off;
	pub = unit_pub_at(u, i);
	pub->name = string_at(u, rd_i32(p), pub_off);
	pub->offset = rd_i32(p + BC_PUB_OFFSET_OFFSET);
	pub->flag = p[BC_PUB_FLAG_OFFSET];

	if (pub->flag > PUB_GLOBAL)
		die_at(unit_name(u), pub_off,
		       "public symbol \"%s\" has invalid flag %u (expected 0 or 1)",
		       pub->name, pub->flag);

	if (pub->flag == PUB_GLOBAL &&
	    (pub->offset < 0 || (size_t)pub->offset >= u->globals_count))
		die_at(unit_name(u), pub_off,
		       "public global \"%s\" index %d is out of range [0, %zu)",
		       pub->name, pub->offset, u->globals_count);

	if (pub->flag == PUB_FUNCTION &&
	    (pub->offset < 0 || (size_t)pub->offset >= u->code_size - 1))
		die_at(unit_name(u), pub_off,
		       "public function \"%s\" code offset %d is out of range",
		       pub->name, pub->offset);

	for (j = 0; j < i; j++)
		if (strcmp(unit_pub_at(u, j)->name, pub->name) == 0)
			die_at(unit_name(u), pub_off,
			       "duplicate public symbol \"%s\"", pub->name);
}

static void unit_parse_publics(struct unit *u, uint64_t imports_end)
{
	size_t handle, id = u->id;
	size_t pub_off;
	size_t i;

	if (u->pubs_count > SIZE_MAX / sizeof(struct pubsym))
		die("too many public symbols in unit %s", unit_name(u));

	handle = meta_alloc_bytes(u->pubs_count * sizeof(struct pubsym));
	u = unit_get(id);
	u->pubs = handle;

	for (i = 0; i < u->pubs_count; i++) {
		pub_off = imports_end + BC_PUB_SIZE * i;
		unit_parse_public(u, i, pub_off);
	}
}

static void unit_register(size_t id)
{
	meta_array_push(&units, BOX(id), "bytecode units");
}

size_t unit_load(const char *path, const char *name)
{
	struct unit_layout layout;
	struct unit *u;
	size_t id;

	id = unit_find_cached(name);
	if (id != UNIT_NONE)
		return id;

	id = unit_create(path, name);
	u = unit_get(id);
	layout = unit_parse_header(u);
	unit_parse_imports(u, layout.strings_end);
	u = unit_get(id);
	unit_parse_publics(u, layout.imports_end);
	unit_register(id);
	return id;
}

struct unit *unit_of_code(const uint8_t *code)
{
	struct unit *u;
	size_t i;

	for (i = 0; i < units.count; i++) {
		u = unit_at(i);
		if (code >= u->code && code < u->code + u->code_size)
			return u;
	}
	die("instruction pointer does not belong to a loaded unit");
}

size_t unit_find_file(const char *name, const char *relative_to)
{
	char path[PATH_MAX];
	const char *dir;
	FILE *f;
	size_t i;

	for (i = 0; i <= search_paths.count; i++) {
		dir = i == 0 ? relative_to : unit_search_path_at(i - 1);
		if (!dir)
			continue;

		snprintf(path, sizeof(path), "%s/%s.bc", dir, name);
		f = fopen(path, "rb");
		if (f) {
			fclose(f);
			return unit_load(path, name);
		}
	}

	die("cannot find bytecode unit \"%s.bc\" (use -I to add search paths)",
	    name);
}

#include "unit.h"

#include "format.h"
#include "meta.h"
#include "reader.h"
#include "unit_internal.h"
#include "util.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { INITIAL_UNIT_CAPACITY = 8, ARRAY_GROWTH_FACTOR = 2 };

struct meta_array {
	size_t handle;
	size_t count;
	size_t capacity;
};

static struct meta_array units;
static struct meta_array search_paths;

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
	const int32_t *imports;

	if (index >= u->imports_count)
		die("invalid import index %zu in unit %s", index, unit_name(u));
	imports = meta_get(u->imports);
	return string_at(u, imports[index], 0);
}

struct pubsym *unit_pub_at(const struct unit *u, size_t index)
{
	if (index >= u->pubs_count)
		die("invalid public symbol index %zu in unit %s", index,
		    unit_name(u));
	return (struct pubsym *)meta_get(u->pubs) + index;
}

static uint8_t *unit_image(const struct unit *u)
{
	return meta_get(u->image);
}

const uint8_t *unit_code(const struct unit *u)
{
	return unit_image(u) + u->code_offset;
}

static size_t read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	size_t handle;
	uint8_t *buf;
	long n;

	if (!f)
		die("cannot open %s: %s", path, strerror(errno));
	if (fseek(f, 0, SEEK_END) != 0)
		die("cannot seek %s", path);

	n = ftell(f);
	if (n < 0)
		die("cannot determine size of %s", path);

	handle = meta_alloc_bytes((size_t)n);
	buf = meta_get(handle);
	if (fseek(f, 0, SEEK_SET) != 0)
		die("cannot seek %s", path);
	if (n > 0 && fread(buf, 1, (size_t)n, f) != (size_t)n)
		die("cannot read %s", path);

	fclose(f);
	buf[n] = 0;
	*size = (size_t)n;
	return handle;
}

const char *string_at(const struct unit *u, int32_t off, size_t where)
{
	const char *s;

	if ((size_t)off >= u->strings_size)
		die_at(unit_name(u), where,
		       "string table offset %d is out of range [0, %zu)", off,
		       u->strings_size);

	s = (const char *)unit_image(u) + BC_HEADER_SIZE + (size_t)off;
	if (!memchr(s, '\0', u->strings_size - (size_t)off))
		die_at(unit_name(u), where,
		       "string at offset %d is not NUL-terminated", off);

	return s;
}

const char *external_string_at(const struct unit *u, int32_t ref, size_t where)
{
	return string_at(u, -(ref + EXTERNAL_REF_BIAS), where);
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
	size_t id, name_handle, path_handle, image_handle;
	size_t image_size;

	name_handle = meta_alloc_string(name);
	path_handle = meta_alloc_string(path);
	id = meta_alloc_bytes(sizeof(*u));
	u = unit_get(id);
	memset(u, 0, sizeof(*u));
	u->id = id;
	u->name = name_handle;
	u->path = path_handle;

	image_handle = read_file(path, &image_size);
	u = unit_get(id);
	u->image = image_handle;
	u->image_size = image_size;
	return id;
}

static struct unit_layout unit_parse_header(struct unit *u)
{
	struct byte_reader reader;
	struct unit_layout layout;
	const uint8_t *magic;
	int32_t version, strings_size, globals_count, imports_count, pubs_count;

	if (u->image_size < BC_HEADER_SIZE)
		die("%s: short bytecode header (%zu bytes, need %d)",
		    unit_path(u), u->image_size, BC_HEADER_SIZE);

	reader_init(&reader, u->id, unit_image(u), u->image_size, 0);
	magic = reader_read(&reader, BC_MAGIC_SIZE);
	if (memcmp(magic, "LaMa", BC_MAGIC_SIZE) != 0)
		die("%s: bad magic, expected \"LaMa\"", unit_path(u));

	version = reader_i32(&reader);
	if (version != BC_FORMAT_VERSION)
		die("%s: unsupported bytecode version %d (expected %d)",
		    unit_path(u), version, BC_FORMAT_VERSION);

	strings_size = reader_i32(&reader);
	globals_count = reader_i32(&reader);
	imports_count = reader_i32(&reader);
	pubs_count = reader_i32(&reader);

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

	layout.strings_end = BC_HEADER_SIZE + (uint64_t)strings_size;
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

	u->strings_size = (size_t)strings_size;
	u->globals_count = (size_t)globals_count;
	u->imports_count = (size_t)imports_count;
	u->pubs_count = (size_t)pubs_count;
	u->code_offset = (size_t)layout.pubs_end;
	u->code_size = u->image_size - layout.pubs_end;

	if (u->code_size > INT32_MAX)
		die("%s: code section is too large: %zu bytes", unit_path(u),
		    u->code_size);
	if (unit_code(u)[u->code_size - 1] != BC_EOF)
		die("%s: code section does not end with the EOF opcode (0xFF)",
		    unit_path(u));

	return layout;
}

static void unit_parse_imports(struct unit *u, uint64_t strings_end)
{
	struct byte_reader reader;
	int32_t *imports;
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
	reader_init(&reader, id, unit_image(u), u->image_size, 0);
	reader_seek(&reader, (size_t)strings_end);
	for (i = 0; i < u->imports_count; i++) {
		import_off = reader.pos;
		so = reader_i32(&reader);
		(void)string_at(u, so, import_off);
		imports[i] = so;
	}
}

static void unit_parse_public(struct unit *u, size_t i, size_t pub_off)
{
	struct byte_reader reader;
	struct pubsym *pub;
	const char *name;
	size_t j;

	reader_init(&reader, u->id, unit_image(u), u->image_size, 0);
	reader_seek(&reader, pub_off);
	pub = unit_pub_at(u, i);
	pub->name = reader_i32(&reader);
	pub->offset = reader_i32(&reader);
	pub->flag = reader_u8(&reader);
	pub->unit_id = u->id;
	name = string_at(u, pub->name, pub_off);

	if (pub->flag > PUB_GLOBAL)
		die_at(unit_name(u), pub_off,
		       "public symbol \"%s\" has invalid flag %u (expected 0 or 1)",
		       name, pub->flag);

	if (pub->flag == PUB_GLOBAL &&
	    (pub->offset < 0 || (size_t)pub->offset >= u->globals_count))
		die_at(unit_name(u), pub_off,
		       "public global \"%s\" index %d is out of range [0, %zu)",
		       name, pub->offset, u->globals_count);

	if (pub->flag == PUB_FUNCTION &&
	    (pub->offset < 0 || (size_t)pub->offset >= u->code_size - 1))
		die_at(unit_name(u), pub_off,
		       "public function \"%s\" code offset %d is out of range",
		       name, pub->offset);

	for (j = 0; j < i; j++)
		if (strcmp(string_at(u, unit_pub_at(u, j)->name, pub_off),
			   name) == 0)
			die_at(unit_name(u), pub_off,
			       "duplicate public symbol \"%s\"", name);
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

size_t unit_find_file(const char *name, const char *relative_to)
{
	char path[PATH_MAX], stable_name[PATH_MAX];
	const char *dir;
	FILE *f;
	size_t i, name_size;
	int n;

	name_size = strlen(name) + 1;
	if (name_size > sizeof(stable_name))
		die("bytecode unit name is too long");
	memcpy(stable_name, name, name_size);
	name = stable_name;

	for (i = 0; i <= search_paths.count; i++) {
		dir = i == 0 ? relative_to : unit_search_path_at(i - 1);
		if (!dir)
			continue;

		n = snprintf(path, sizeof(path), "%s/%s.bc", dir, name);
		if (n < 0 || (size_t)n >= sizeof(path))
			die("bytecode path for unit \"%s\" is too long", name);
		f = fopen(path, "rb");
		if (f) {
			fclose(f);
			return unit_load(path, name);
		}
	}

	die("cannot find bytecode unit \"%s.bc\" (use -I to add search paths)",
	    name);
}

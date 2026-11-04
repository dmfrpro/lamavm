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

enum { INITIAL_UNIT_CAPACITY = 8 };

struct unit **units;
size_t units_count;
static size_t units_cap;
const char *search_paths[MAX_SEARCH_PATHS];
size_t search_paths_count;

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
		die_at(u->name, where,
		       "string table offset %d is out of range [0, %zu)", off,
		       u->strings_size);

	s = u->strings + off;
	if (!memchr(s, '\0', u->strings_size - (size_t)off))
		die_at(u->name, where,
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

static struct unit *unit_find_cached(const char *name)
{
	size_t i;

	for (i = 0; i < units_count; i++)
		if (strcmp(units[i]->name, name) == 0)
			return units[i];

	return NULL;
}

static struct unit *unit_create(const char *path, const char *name)
{
	struct unit *u;

	u = xmalloc(sizeof(*u));
	memset(u, 0, sizeof(*u));
	u->name = xstrdup(name);
	u->path = xstrdup(path);
	u->image = read_file(path, &u->image_size);
	return u;
}

static struct unit_layout unit_parse_header(struct unit *u)
{
	struct unit_layout layout;
	uint64_t off;
	int32_t version, strings_size, globals_count, imports_count, pubs_count;

	if (u->image_size < BC_HEADER_SIZE)
		die("%s: short bytecode header (%zu bytes, need %d)", u->path,
		    u->image_size, BC_HEADER_SIZE);
	if (memcmp(u->image, "LaMa", BC_MAGIC_SIZE) != 0)
		die("%s: bad magic, expected \"LaMa\"", u->path);

	version = rd_i32(u->image + BC_VERSION_OFFSET);
	if (version != BC_FORMAT_VERSION)
		die("%s: unsupported bytecode version %d (expected %d)",
		    u->path, version, BC_FORMAT_VERSION);

	strings_size = rd_i32(u->image + BC_STRINGS_SIZE_OFFSET);
	globals_count = rd_i32(u->image + BC_GLOBALS_COUNT_OFFSET);
	imports_count = rd_i32(u->image + BC_IMPORTS_COUNT_OFFSET);
	pubs_count = rd_i32(u->image + BC_PUBS_COUNT_OFFSET);

	if (strings_size < 0)
		die("%s: negative string table size %d", u->path, strings_size);
	if (globals_count < 0)
		die("%s: negative globals count %d", u->path, globals_count);
	if (imports_count < 0)
		die("%s: negative imports count %d", u->path, imports_count);
	if (pubs_count < 0)
		die("%s: negative public symbols count %d", u->path,
		    pubs_count);

	off = BC_HEADER_SIZE;
	layout.strings_end = off + (uint64_t)strings_size;
	layout.imports_end = layout.strings_end +
			     (uint64_t)BC_IMPORT_SIZE * (uint64_t)imports_count;
	layout.pubs_end = layout.imports_end +
			  (uint64_t)BC_PUB_SIZE * (uint64_t)pubs_count;

	if (layout.pubs_end > u->image_size)
		die("%s: truncated file: need %llu bytes, have %zu", u->path,
		    (unsigned long long)layout.pubs_end, u->image_size);
	if (layout.pubs_end == u->image_size)
		die("%s: missing code section", u->path);

	u->strings = (const char *)u->image + off;
	u->strings_size = (size_t)strings_size;
	u->globals_count = (size_t)globals_count;
	u->imports_count = (size_t)imports_count;
	u->pubs_count = (size_t)pubs_count;
	u->code = u->image + layout.pubs_end;
	u->code_size = u->image_size - layout.pubs_end;

	if (u->code[u->code_size - 1] != BC_EOF)
		die("%s: code section does not end with the EOF opcode (0xFF)",
		    u->path);

	return layout;
}

static void unit_parse_imports(struct unit *u, uint64_t strings_end)
{
	size_t import_off;
	size_t i;
	int32_t so;

	u->imports = xmalloc(u->imports_count * sizeof(*u->imports));
	for (i = 0; i < u->imports_count; i++) {
		import_off = strings_end + BC_IMPORT_SIZE * i;
		so = rd_i32(u->image + import_off);
		u->imports[i] = string_at(u, so, import_off);
	}
}

static void unit_parse_public(struct unit *u, size_t i, size_t pub_off)
{
	const uint8_t *p;
	struct pubsym *pub;
	size_t j;

	p = u->image + pub_off;
	pub = &u->pubs[i];
	pub->name = string_at(u, rd_i32(p), pub_off);
	pub->offset = rd_i32(p + BC_PUB_OFFSET_OFFSET);
	pub->flag = p[BC_PUB_FLAG_OFFSET];

	if (pub->flag > PUB_GLOBAL)
		die_at(u->name, pub_off,
		       "public symbol \"%s\" has invalid flag %u (expected 0 or 1)",
		       pub->name, pub->flag);
	if (pub->flag == PUB_GLOBAL &&
	    (pub->offset < 0 || (size_t)pub->offset >= u->globals_count))
		die_at(u->name, pub_off,
		       "public global \"%s\" index %d is out of range [0, %zu)",
		       pub->name, pub->offset, u->globals_count);
	if (pub->flag == PUB_FUNCTION &&
	    (pub->offset < 0 || (size_t)pub->offset >= u->code_size - 1))
		die_at(u->name, pub_off,
		       "public function \"%s\" code offset %d is out of range",
		       pub->name, pub->offset);

	for (j = 0; j < i; j++)
		if (strcmp(u->pubs[j].name, pub->name) == 0)
			die_at(u->name, pub_off,
			       "duplicate public symbol \"%s\"", pub->name);
}

static void unit_parse_publics(struct unit *u, uint64_t imports_end)
{
	size_t pub_off;
	size_t i;

	u->pubs = xmalloc(u->pubs_count * sizeof(*u->pubs));
	for (i = 0; i < u->pubs_count; i++) {
		pub_off = imports_end + BC_PUB_SIZE * i;
		unit_parse_public(u, i, pub_off);
	}
}

static void unit_register(struct unit *u)
{
	if (units_count == units_cap) {
		units_cap = units_cap ? units_cap * ARRAY_GROWTH_FACTOR :
					INITIAL_UNIT_CAPACITY;
		units = xrealloc(units, units_cap * sizeof(*units));
	}

	units[units_count++] = u;
}

struct unit *unit_load(const char *path, const char *name)
{
	struct unit_layout layout;
	struct unit *u;

	u = unit_find_cached(name);
	if (u)
		return u;

	u = unit_create(path, name);
	layout = unit_parse_header(u);
	unit_parse_imports(u, layout.strings_end);
	unit_parse_publics(u, layout.imports_end);
	unit_register(u);
	return u;
}

struct unit *unit_find_file(const char *name, const char *relative_to)
{
	char path[PATH_MAX];
	const char *dir;
	FILE *f;
	size_t i;

	for (i = 0; i <= search_paths_count; i++) {
		dir = i == 0 ? relative_to : search_paths[i - 1];
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

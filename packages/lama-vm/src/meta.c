#include "meta.h"

#include "util.h"

#include <stdint.h>
#include <string.h>

enum { INITIAL_ROOT_CAPACITY = 8, ARRAY_GROWTH_FACTOR = 2 };

static aint *meta_roots;
static size_t meta_roots_count;
static size_t meta_roots_cap;

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

void meta_init(void)
{
	push_extra_root((void **)&meta_roots);
	meta_roots = alloc_array_contents(INITIAL_ROOT_CAPACITY);
	meta_roots_cap = INITIAL_ROOT_CAPACITY;
}

size_t meta_alloc_bytes(size_t n)
{
	data *obj;

	reserve_handle();
	obj = alloc_string(n);
	if (!obj)
		die("out of memory allocating %zu bytes of decoder metadata",
		    n);
	return store_object(obj->contents);
}

size_t meta_alloc_array(size_t n)
{
	check_array_size(n);
	reserve_handle();
	return store_object(alloc_array_contents(n));
}

void *meta_get(size_t handle)
{
	if (handle >= meta_roots_count || meta_roots[handle] == BOX(0))
		die("invalid decoder metadata handle %zu", handle);
	return (void *)meta_roots[handle];
}

size_t meta_alloc_string(const char *s)
{
	size_t handle;
	size_t n = strlen(s);

	handle = meta_alloc_bytes(n);
	memcpy(meta_get(handle), s, n + 1);
	return handle;
}

void meta_replace_array(size_t handle, size_t n)
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

struct meta_scope meta_scope_begin(void)
{
	return (struct meta_scope){ .count = meta_roots_count };
}

void meta_scope_end(struct meta_scope scope)
{
	if (scope.count > meta_roots_count)
		die("invalid decoder metadata scope");
	while (meta_roots_count > scope.count)
		meta_roots[--meta_roots_count] = BOX(0);
}

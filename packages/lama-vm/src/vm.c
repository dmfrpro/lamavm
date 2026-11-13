#include "decode.h"
#include "execute.h"
#include "link.h"
#include "unit.h"
#include "util.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dir_of(const char *path, char dir[PATH_MAX])
{
	const char *slash = strrchr(path, '/');
	size_t n;

	if (!slash) {
		memcpy(dir, ".", sizeof("."));
		return;
	}

	n = (size_t)(slash - path);
	if (n >= PATH_MAX)
		die("bytecode directory path is too long");
	memcpy(dir, path, n);
	dir[n] = '\0';
}

static void unit_name_of(const char *path, char name[PATH_MAX])
{
	const char *base = path_basename(path);
	size_t n = strlen(base);
	const size_t extension_len = sizeof(".bc") - 1;

	if (n >= PATH_MAX)
		die("bytecode unit name is too long");
	memcpy(name, base, n + 1);
	if (n > extension_len && strcmp(name + n - extension_len, ".bc") == 0)
		name[n - extension_len] = '\0';
}

int main(int argc, char **argv)
{
	const char *file = NULL;
	struct unit *u;
	char dir[PATH_MAX], name[PATH_MAX];
	aint *globals;
	size_t main_unit, dep;
	size_t total_globals = 0;
	size_t goff = 0;
	size_t i, j, count;
	int argi;

	execute_stack_init(0);
	__init();
	unit_init();

	for (argi = 1; argi < argc; argi++) {
		if (strcmp(argv[argi], "-I") == 0) {
			if (++argi >= argc)
				die("option -I requires a directory argument");
			unit_add_search_path(argv[argi]);
		} else if (strcmp(argv[argi], "--") == 0) {
			argi++;
			break;
		} else if (!file) {
			file = argv[argi];
		} else {
			break;
		}
	}

	if (!file) {
		fprintf(stderr, "usage: %s [-I dir]... file.bc [-- args...]\n",
			progname);
		return EXIT_FAILURE;
	}

	unit_name_of(file, name);
	main_unit = unit_load(file, name);
	unit_validate(unit_get(main_unit));

	for (i = 0; i < unit_count(); i++) {
		u = unit_at(i);
		dir_of(unit_path(u), dir);
		count = u->imports_count;
		for (j = 0; j < count; j++) {
			const char *import;

			u = unit_at(i);
			import = unit_import_at(u, j);
			if (strcmp(import, "Std") == 0)
				continue;

			dep = unit_find_file(import, dir);
			if (!unit_get(dep)->validated)
				unit_validate(unit_get(dep));
		}
	}

	for (i = 0; i < unit_count(); i++)
		total_globals += unit_at(i)->globals_count;

	globals = execute_stack_init(total_globals);
	for (i = 0; i < unit_count(); i++) {
		u = unit_at(i);
		u->globals = globals + goff;
		for (j = 0; j < u->globals_count; j++)
			u->globals[j] = BOX(0);

		goff += u->globals_count;
	}

	link_units();
	set_args(argc - argi, argv + argi);

	run(unit_get(main_unit));

	__shutdown();
	return EXIT_SUCCESS;
}

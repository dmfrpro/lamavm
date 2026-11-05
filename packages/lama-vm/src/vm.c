#include "decode.h"
#include "execute.h"
#include "link.h"
#include "unit.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dir_of(const char *path)
{
	const char *slash = strrchr(path, '/');

	if (!slash)
		return xstrdup(".");

	return strndup(path, (size_t)(slash - path));
}

static char *unit_name_of(const char *path)
{
	const char *base = path_basename(path);
	char *name = xstrdup(base);
	size_t n = strlen(name);
	const size_t extension_len = sizeof(".bc") - 1;

	if (n > extension_len && strcmp(name + n - extension_len, ".bc") == 0)
		name[n - extension_len] = '\0';

	return name;
}

int main(int argc, char **argv)
{
	const char *file = NULL;
	struct unit *main_unit;
	struct unit *u;
	struct unit *dep;
	char *dir, *name;
	aint *globals;
	size_t total_globals = 0;
	size_t goff = 0;
	size_t i, j;
	int argi;

	for (argi = 1; argi < argc; argi++) {
		if (strcmp(argv[argi], "-I") == 0) {
			if (++argi >= argc)
				die("option -I requires a directory argument");
			if (search_paths_count == MAX_SEARCH_PATHS)
				die("too many -I options");

			search_paths[search_paths_count++] = argv[argi];
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

	name = unit_name_of(file);
	main_unit = unit_load(file, name);
	free(name);
	unit_decode(main_unit);

	for (i = 0; i < units_count; i++) {
		u = units[i];
		dir = dir_of(u->path);
		u->import_units =
			xmalloc(u->imports_count * sizeof(*u->import_units));

		for (j = 0; j < u->imports_count; j++) {
			if (strcmp(u->imports[j], "Std") == 0)
				continue;

			dep = unit_find_file(u->imports[j], dir);
			u->import_units[j] = dep;
			if (!dep->insns)
				unit_decode(dep);
		}

		free(dir);
	}

	for (i = 0; i < units_count; i++)
		total_globals += units[i]->globals_count;

	globals = execute_stack_init(total_globals);
	for (i = 0; i < units_count; i++) {
		units[i]->globals = globals + goff;
		for (j = 0; j < units[i]->globals_count; j++)
			units[i]->globals[j] = BOX(0);

		goff += units[i]->globals_count;
	}

	link_units();
	__init();
	set_args(argc - argi, argv + argi);

	run(&main_unit->insns[0]);

	__shutdown();
	return EXIT_SUCCESS;
}

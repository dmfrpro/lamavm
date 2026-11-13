#include "util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const progname = "lama-custom-vm";

_Noreturn void die(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", progname);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(EXIT_FAILURE);
}

_Noreturn void die_at(const char *unit, size_t off, const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "%s: %s: bytecode offset 0x%zx: ", progname, unit, off);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(EXIT_FAILURE);
}

void *xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);

	if (!p)
		die("out of memory (%zu bytes)", n);
	return p;
}

const char *path_basename(const char *p)
{
	const char *s = strrchr(p, '/');

	return s ? s + 1 : p;
}

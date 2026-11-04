#include "builtins.h"

#include <string.h>

static inline aint wrap_read(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Lread();
}

static inline aint wrap_write(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Lwrite(args[0]);
}

static inline aint wrap_length(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Llength((void *)args[0]);
}

static inline aint wrap_string(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Lstring(args);
}

static inline aint wrap_stringcat(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Lstringcat(args);
}

static inline aint wrap_substring(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Lsubstring(args);
}

static inline aint wrap_clone(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Lclone(args);
}

static inline aint wrap_elem(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Belem((void *)args[0], args[1]);
}

static inline aint wrap_array(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Barray(args, BOX(n));
}

static inline aint wrap_concat(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Li__Infix_4343(args);
}

static inline aint wrap_cons(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Ls__Infix_58((void **)args);
}

static inline aint wrap_compare(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Lcompare((void *)args[0], (void *)args[1]);
}

static inline aint wrap_flatCompare(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)LflatCompare((void *)args[0], (void *)args[1]);
}

static inline aint wrap_hash(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Lhash((void *)args[0]);
}

static inline aint wrap_stringInt(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)LstringInt((char *)args[0]);
}

static inline aint wrap_uppercase(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Luppercase((void *)args[0]);
}

static inline aint wrap_lowercase(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Llowercase((void *)args[0]);
}

static inline aint wrap_matchSubString(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)LmatchSubString((char *)args[0], (char *)args[1], args[2]);
}

static inline aint wrap_regexp(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Lregexp((char *)args[0]);
}

static inline aint wrap_regexpMatch(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)LregexpMatch((regex_t *)args[0], (char *)args[1], args[2]);
}

static inline aint wrap_random(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Lrandom(args[0]);
}

static inline aint wrap_time(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Ltime();
}

static inline aint wrap_fst(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Belem((void *)args[0], BOX(0));
}

static inline aint wrap_snd(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Belem((void *)args[0], BOX(1));
}

static inline aint wrap_hd(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Belem((void *)args[0], BOX(0));
}

static inline aint wrap_tl(aint *args, int n)
{
	(void)args;
	(void)n;
	return (aint)Belem((void *)args[0], BOX(1));
}

static const struct builtin builtins[] = {
	{ "read", wrap_read },
	{ "write", wrap_write },
	{ "length", wrap_length },
	{ "string", wrap_string },
	{ "stringcat", wrap_stringcat },
	{ "substring", wrap_substring },
	{ "clone", wrap_clone },
	{ ".elem", wrap_elem },
	{ ".array", wrap_array },
	{ "i__Infix_4343", wrap_concat },
	{ "s__Infix_58", wrap_cons },
	{ "compare", wrap_compare },
	{ "flatCompare", wrap_flatCompare },
	{ "hash", wrap_hash },
	{ "stringInt", wrap_stringInt },
	{ "uppercase", wrap_uppercase },
	{ "lowercase", wrap_lowercase },
	{ "matchSubString", wrap_matchSubString },
	{ "regexp", wrap_regexp },
	{ "regexpMatch", wrap_regexpMatch },
	{ "random", wrap_random },
	{ "time", wrap_time },
	{ "fst", wrap_fst },
	{ "snd", wrap_snd },
	{ "hd", wrap_hd },
	{ "tl", wrap_tl },
};

const struct builtin *builtin_find(const char *name)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(builtins); i++)
		if (strcmp(builtins[i].name, name) == 0)
			return &builtins[i];

	return NULL;
}

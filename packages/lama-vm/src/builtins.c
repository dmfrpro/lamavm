#include "builtins.h"

#include <stdlib.h>
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
	{ ".array", wrap_array },
	{ ".elem", wrap_elem },
	{ "clone", wrap_clone },
	{ "compare", wrap_compare },
	{ "flatCompare", wrap_flatCompare },
	{ "fst", wrap_fst },
	{ "hash", wrap_hash },
	{ "hd", wrap_hd },
	{ "i__Infix_4343", wrap_concat },
	{ "length", wrap_length },
	{ "lowercase", wrap_lowercase },
	{ "matchSubString", wrap_matchSubString },
	{ "random", wrap_random },
	{ "read", wrap_read },
	{ "regexp", wrap_regexp },
	{ "regexpMatch", wrap_regexpMatch },
	{ "s__Infix_58", wrap_cons },
	{ "snd", wrap_snd },
	{ "string", wrap_string },
	{ "stringInt", wrap_stringInt },
	{ "stringcat", wrap_stringcat },
	{ "substring", wrap_substring },
	{ "time", wrap_time },
	{ "tl", wrap_tl },
	{ "uppercase", wrap_uppercase },
	{ "write", wrap_write },
};

static int builtin_compare(const void *key, const void *value)
{
	const char *name = key;
	const struct builtin *bi = value;

	return strcmp(name, bi->name);
}

const struct builtin *builtin_find(const char *name)
{
	return bsearch(name, builtins, ARRAY_SIZE(builtins), sizeof(*builtins),
		       builtin_compare);
}

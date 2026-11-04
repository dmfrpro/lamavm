#ifndef LAMA_VM_RUNTIME_H
#define LAMA_VM_RUNTIME_H

#include <regex.h>
#include <stddef.h>

#include "runtime_common.h"
#include "gc.h"

extern size_t __gc_stack_top, __gc_stack_bottom;
extern void set_args(aint argc, char *argv[]);

extern aint Lread(void);
extern aint Lwrite(aint n);
extern aint Llength(void *p);
extern aint LtagHash(char *s);
extern void *Bstring(aint *args);
extern void *Lstring(aint *args);
extern void *Lstringcat(aint *args);
extern void *Lsubstring(aint *args);
extern void *Lclone(aint *args);
extern void *Barray(aint *args, aint bn);
extern void *Bsexp(aint *args, aint bn);
extern void *Bclosure(aint *args, aint bn);
extern void *Belem(void *p, aint i);
extern void *Bsta(void *x, aint i, void *v);
extern aint Btag(void *d, aint t, aint n);
extern aint Barray_patt(void *d, aint n);
extern aint Bstring_patt(void *x, void *y);
extern aint Bclosure_tag_patt(void *x);
extern aint Bboxed_patt(void *x);
extern aint Bunboxed_patt(void *x);
extern aint Barray_tag_patt(void *x);
extern aint Bstring_tag_patt(void *x);
extern aint Bsexp_tag_patt(void *x);
extern void Bmatch_failure(void *v, char *fname, aint line, aint col);
extern aint Lcompare(void *p, void *q);
extern aint LflatCompare(void *p, void *q);
extern aint Lhash(void *p);
extern void *LstringInt(char *b);
extern aint Luppercase(void *v);
extern aint Llowercase(void *v);
extern aint LmatchSubString(char *subj, char *patt, aint pos);
extern regex_t *Lregexp(char *regexp);
extern aint LregexpMatch(regex_t *b, char *s, aint pos);
extern aint Lrandom(aint n);
extern aint Ltime(void);
extern aint Li__Infix_4343(aint *args);
extern void *Ls__Infix_58(void **args);

_Noreturn void failure(char *s, ...);

#endif

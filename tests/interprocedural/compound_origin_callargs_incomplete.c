/*
 * Incident #64 regression (call-arguments containment form): the
 * pre-fix return handler resolved borrow origins from the FIRST parameter
 * contained anywhere in the return expression BEFORE trying the callee
 * mapping, so `return dup(p)` was claimed as a borrow of p while dup()
 * returns a fresh allocation. Callers then bound the result to a wrong
 * borrow (B003 artifacts) or lost the ownership fact. Must never PASS.
 */
#include <stdlib.h>
static int *dupit(int *x) {   /* owned return: fresh allocation */
    int *r = malloc(sizeof(int));
    if (!r) return NULL;
    *r = *x;
    return r;
}
static int *wrap(int *p) {    /* pre-fix: claimed borrow_from_arg@0 */
    return dupit(p);
}
int main(void) {
    int v = 5;
    int *x = malloc(sizeof(int));
    if (!x) return 1;
    *x = v;
    int *q = wrap(x);   /* q is a fresh allocation, NOT a borrow of x */
    free(x);            /* destroying x must not keep q modelled valid */
    *q = 1;             /* q is valid here (fresh); the summary must be
                           Unknown so this stays incomplete, never PASS */
    free(q);
    return 0;
}

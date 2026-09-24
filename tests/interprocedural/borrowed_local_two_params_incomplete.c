#include <stdlib.h>

/*
 * Milestone #73 (ADR-0031), Area R: two-parameter join is never a
 * singleton.
 *
 * `v = p; if (c) v = q; return v;` must join the origin sets to
 * {0,1} -> Unknown return effect. A last-write-wins reading would
 * produce a wrong singleton and let a caller bind the returned borrow
 * to the wrong parameter's object (false-PASS vector, plan risk 5).
 *
 * Required verdicts:
 *   TODAY (pre-fix): INCOMPLETE (never a singleton).
 *   AFTER  (post-fix): INCOMPLETE.
 */
static int *pick(int *p, int *q, int c) {
    int *v = p;
    if (c) v = q;
    return v;
}

int main(void) {
    int *a = malloc(sizeof(int));
    int *b = malloc(sizeof(int));
    if (!a || !b) { free(a); free(b); return 1; }
    int *r = pick(a, b, 1);
    *r = 1;
    free(a);
    free(b);
    return 0;
}

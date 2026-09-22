#include <stdlib.h>

/*
 * Incident #62 regression: borrow-origin misattribution through parameter
 * reassignment.
 *
 * reassign() returns the object passed as its SECOND argument, but the
 * pre-fix summary path attributed the returned borrow origin to the
 * syntactically referenced parameter name (arg 0). The caller then
 * destroyed the true origin (b) while the returned borrow was keyed to a,
 * and the use of q after destroy(b) received PASS with zero obligations
 * (ASan confirms heap-use-after-free at runtime).
 *
 * A pointer parameter that is assigned anywhere in the body is not a sound
 * borrow origin; the return effect must fail closed to Unknown, so this
 * case is INCOMPLETE, never PASS.
 */

static void use(int *p) { (void)*p; }
static void destroy(int *p) { free(p); }

static int *reassign(int *p, int *r) { p = r; return p; }

int run(void) {
    int *a = malloc(sizeof(int));
    int *b = malloc(sizeof(int));
    if (!a || !b) return 1;
    int *q = reassign(a, b);
    destroy(b); /* destroys the object q actually borrows */
    use(q);     /* temporal violation: use after destroy */
    destroy(a);
    return 0;
}

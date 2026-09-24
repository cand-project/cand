#include <stdlib.h>

/*
 * Milestone #73 (ADR-0031), Area C: two-step pointer-difference delta.
 *
 * `long d = q - p; p += d;` stores the pointer difference in a scalar
 * first, so the advancing assignment itself only mentions an integer
 * variable. Under the Area C rule the delta preserves the parent
 * (relation Interior) because a well-defined `q - p` implies q points
 * into the same array as p; the subsequent free of the interior cursor
 * is then flagged by the exact-base-required destruction predicate.
 * This fixture pins that composed behavior with a DOCUMENTED expected
 * verdict rather than leaving it implicit (plan risk list).
 *
 * NOTE: as a static-analysis fixture this deliberately exercises the
 * rebind shape across distinct allocations; the analyzer never executes
 * the translation unit.
 *
 * Required verdicts:
 *   TODAY (pre-fix): INCOMPLETE.
 *   AFTER  (post-fix): INCOMPLETE (free-of-interior obligation; never
 *                      a PASS).
 */
static void two_step_free(char *base, char *q) {
    char *p = base;
    long d = q - p; /* stored pointer difference */
    p += d;
    free(p);
}

int main(void) {
    char *a = malloc(32);
    char *b = malloc(32);
    if (!a || !b) return 1;
    two_step_free(a, b);
    free(a);
    return 0;
}

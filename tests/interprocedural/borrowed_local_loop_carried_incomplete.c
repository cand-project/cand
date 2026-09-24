#include <stdlib.h>

/*
 * Milestone #73 (ADR-0031), Area R: loop-carried reassignment between
 * two parameters' pointers.
 *
 * The returned local is reassigned between the two parameters inside a
 * loop; the dataflow must iterate to a monotone-join fixpoint
 * ({0,1} -> Unknown), never a last-write-wins singleton (plan risk 5).
 *
 * Required verdicts:
 *   TODAY (pre-fix): INCOMPLETE.
 *   AFTER  (post-fix): INCOMPLETE.
 */
static int *loop_pick(int *p, int *q, int n) {
    int *v = p;
    for (int i = 0; i < n; i++) {
        if (i & 1) v = q;
        else v = p;
    }
    return v;
}

int main(void) {
    int *a = malloc(sizeof(int));
    int *b = malloc(sizeof(int));
    if (!a || !b) { free(a); free(b); return 1; }
    int *r = loop_pick(a, b, 4);
    *r = 1;
    free(a);
    free(b);
    return 0;
}

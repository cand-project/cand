/*
 * Milestone #54 / ADR-0028 documented boundary (residual debt): the
 * destroyed local aliases the parameter only transitively (through
 * another local), outside the single-step bounded trace. KNOWN FALSE
 * FAIL, recorded in docs/pilots/ALIAS-STORAGE-PARETO.md section 4.
 */
#include <stdlib.h>
static void run(int *p) { int *q = p; int *r = q; free(r); }
int main(void) {
    int *x = malloc(sizeof *x);
    if (!x) return 1;
    run(x);
    return 0;
}

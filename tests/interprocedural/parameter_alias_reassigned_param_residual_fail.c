/*
 * Milestone #54 / ADR-0028 documented boundary (residual debt): the
 * parameter is reassigned in the body (`p = NULL` ownership-transfer
 * idiom), so the bounded trace fails closed and the pre-repair
 * behavior is preserved. This is a KNOWN FALSE FAIL, recorded in
 * docs/pilots/ALIAS-STORAGE-PARETO.md section 4 (adjacent to #25).
 * The fixture pins the boundary so any future change is deliberate.
 */
#include <stdlib.h>
static void run(int *p) { int *q = p; p = NULL; free(q); }
int main(void) {
    int *x = malloc(sizeof *x);
    if (!x) return 1;
    run(x);
    return 0;
}

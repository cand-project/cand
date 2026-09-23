/*
 * Milestone #54 / ADR-0028: destruction of a parameter through a
 * single-assignment declaration-init local alias is attributed to the
 * parameter (same summary as the direct `free(p)` form). The caller
 * passes an owned pointer and does not use it after: legal program,
 * must PASS. Before the repair this was a false FAIL
 * (CAND-O006 borrowed-parameter-destroyed).
 */
#include <stdlib.h>
static void run(int *p) { int *q = p; free(q); }
int main(void) {
    int *x = malloc(sizeof *x);
    if (!x) return 1;
    run(x);
    return 0;
}

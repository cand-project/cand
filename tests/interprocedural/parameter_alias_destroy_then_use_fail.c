/*
 * Milestone #54 / ADR-0028: use after destruction through the alias.
 * With the false FAIL repaired, the genuine use-after-destroy must be
 * reported at the correct locus (previously masked by CAND-O006).
 */
#include <stdlib.h>
static int run(int *p) { int *q = p; free(q); return *q; }
int main(void) {
    int *x = malloc(sizeof *x);
    if (!x) return 1;
    return run(x);
}

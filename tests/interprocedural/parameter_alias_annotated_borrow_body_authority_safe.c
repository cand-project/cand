/*
 * Milestone #54 / ADR-0028 annotation-parity control: a consuming body
 * is authoritative over a cand:borrow_shared annotation for the direct
 * free form (see parameter_borrow_free_invalid.c, whose FAIL comes
 * from the use-after, not the free). The alias form must produce the
 * identical verdict and summary (Destroy), not a CAND-O006. Both
 * forms are pinned in one file with tracked-local callers.
 */
#include <stdlib.h>
#define CAND_A(value) __attribute__((annotate(value)))
#define CAND_BORROW CAND_A("cand:borrow_shared")
static int invalid_direct(int *p CAND_BORROW) { free(p); return 0; }
static int invalid_alias(int *p CAND_BORROW) { int *q = p; free(q); return 0; }
int main(void) {
    int *x = malloc(sizeof *x);
    int *y = malloc(sizeof *y);
    if (!x || !y) return 1;
    return invalid_direct(x) + invalid_alias(y);
}

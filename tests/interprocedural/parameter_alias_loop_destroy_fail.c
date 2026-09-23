/*
 * Milestone #54 / ADR-0028: loop destruction through an alias. The
 * summary joins to Unknown (conditional context), the in-flow destroy
 * proceeds, and the second iteration is a double destruction FAIL —
 * strictly stronger than the pre-repair single CAND-O006.
 */
#include <stdlib.h>
static void run(int *p) { for (int i = 0; i < 2; ++i) { int *q = p; free(q); } }
int main(void) {
    int *x = malloc(sizeof *x);
    if (!x) return 1;
    run(x);
    return 0;
}

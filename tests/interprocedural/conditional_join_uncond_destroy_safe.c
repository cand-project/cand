/*
 * Milestone #61 / ADR-0027: conditional borrow joined with an
 * unconditional destroy yields destroy (the function always frees), which
 * is a true summary; the caller that never touches the pointer after the
 * call passes. The destroy direction is not weakened by the join.
 */
#include <stdlib.h>
static void borrow_use(int *p) { int x = *p; (void)x; }
/* conditional borrow + unconditional destroy: conflict -> Unknown */
static void f(int *p, int c) { if (c) borrow_use(p); free(p); }
int main(void) {
    int *q = malloc(sizeof(int));
    if (!q) return 1;
    f(q, 1);
    return 0;
}

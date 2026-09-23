/*
 * Milestone #54 / ADR-0028 mis-attribution probe (companion): the
 * destruction is attributed to `a`; using `a`'s argument after the
 * call must FAIL. A rule that resolved the alias to the wrong (or no)
 * parameter would turn this into a PASS (false PASS) or an
 * INCOMPLETE (coverage gap).
 */
#include <stdlib.h>
static void run(int *a, int *b) { int *q = a; free(q); (void)b; }
int main(void) {
    int *x = malloc(sizeof *x);
    int *y = malloc(sizeof *y);
    if (!x || !y) return 1;
    run(x, y);
    return *x;
}

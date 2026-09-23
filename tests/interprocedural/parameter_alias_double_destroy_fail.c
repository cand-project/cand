/*
 * Milestone #54 / ADR-0028: alias destroy followed by the direct free
 * of the same parameter is an in-function double destruction.
 */
#include <stdlib.h>
static void run(int *p) { int *q = p; free(q); free(p); }
int main(void) {
    int *x = malloc(sizeof *x);
    if (!x) return 1;
    run(x);
    return 0;
}

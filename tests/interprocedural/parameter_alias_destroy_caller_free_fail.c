/*
 * Milestone #54 / ADR-0028: freeing the argument after a Destroy
 * callee is a double destruction (the body frees it through the
 * alias).
 */
#include <stdlib.h>
static void run(int *p) { int *q = p; free(q); }
int main(void) {
    int *x = malloc(sizeof *x);
    if (!x) return 1;
    run(x);
    free(x);
    return 0;
}

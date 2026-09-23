/*
 * Milestone #54 / ADR-0028: the callee's Destroy summary must reach
 * the caller: using the argument after the call is a use-after-destroy
 * FAIL (the object really is freed through the alias).
 */
#include <stdlib.h>
static void run(int *p) { int *q = p; free(q); }
int main(void) {
    int *x = malloc(sizeof *x);
    if (!x) return 1;
    run(x);
    return *x;
}

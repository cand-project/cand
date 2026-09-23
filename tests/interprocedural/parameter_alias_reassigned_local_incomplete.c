/*
 * Milestone #54 / ADR-0028 fail-closed control: the alias local is
 * reassigned between the parameter copy and the destruction, so the
 * bounded trace must not resolve it (and must not attribute the
 * destruction to the parameter).
 */
#include <stdlib.h>
static void run(int *p) { int *q = p; q = malloc(sizeof *q); free(q); }
int main(void) {
    int *x = malloc(sizeof *x);
    if (!x) return 1;
    run(x);
    return *x;
}

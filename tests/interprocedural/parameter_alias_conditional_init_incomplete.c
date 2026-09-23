/*
 * Milestone #54 / ADR-0028 fail-closed control: the alias initializer
 * references two parameters, so no single-parameter attribution is
 * sound; the destruction stays blocked (fail-closed), never resolved.
 */
#include <stdlib.h>
static void run(int *a, int *b, int c) { int *q = c ? a : b; free(q); }
int main(void) {
    int *x = malloc(sizeof *x);
    int *y = malloc(sizeof *y);
    if (!x || !y) return 1;
    run(x, y, 1);
    free(x);
    free(y);
    return 0;
}

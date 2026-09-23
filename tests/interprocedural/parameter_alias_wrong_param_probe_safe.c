/*
 * Milestone #54 / ADR-0028 mis-attribution probe: the alias resolves
 * to parameter `a` only. `b`'s object is untouched by the destruction,
 * so using it after the call is legal and must PASS. (If the rule
 * wrongly destroyed `b`, this would become a false FAIL; if it
 * destroyed neither, the companion _fail probe would stop failing.)
 */
#include <stdlib.h>
static void run(int *a, int *b) { int *q = a; free(q); (void)b; }
int main(void) {
    int *x = malloc(sizeof *x);
    int *y = malloc(sizeof *y);
    if (!x || !y) return 1;
    run(x, y);
    return *y;
}

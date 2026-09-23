/*
 * Incident #64 regression (pointer-condition form): the resolved "first
 * contained parameter" was the condition pointer p itself, so destroying
 * branch a's origin went unnoticed. Must never PASS.
 */
#include <stdlib.h>
static int *pick(int *p, int *a, int *b) { return p ? a : b; }
int main(void) {
    int *w = malloc(sizeof(int));
    int *x = malloc(sizeof(int));
    int *y = malloc(sizeof(int));
    if (!w || !x || !y) return 1;
    int *q = pick(w, x, y);   /* p non-null -> returns a == x */
    free(x);                  /* destroys the object q actually is */
    *q = 1;                   /* temporal violation */
    free(w); free(y);
    return 0;
}

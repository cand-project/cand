/*
 * Incident #64 regression: compound return expression misattributes the
 * borrow origin. pick() returns b's object when c==0, but the pre-fix
 * summary path resolved the origin from the FIRST parameter contained
 * anywhere in the conditional operator (a). A caller that destroyed the
 * true origin and used the returned pointer received PASS (ASan-confirmed
 * use-after-free). Must never PASS.
 */
#include <stdlib.h>
static int *pick(int *a, int *b) { int c = 0; return c ? a : b; }
int main(void) {
    int *x = malloc(sizeof(int));
    int *y = malloc(sizeof(int));
    if (!x || !y) return 1;
    int *q = pick(x, y);
    free(y);       /* destroys the object q actually borrows (c==0 -> b) */
    *q = 1;        /* temporal violation */
    free(x);
    return 0;
}

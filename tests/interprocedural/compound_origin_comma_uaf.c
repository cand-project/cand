/*
 * Incident #64 regression (comma form): the discarded left side of the
 * comma references a, so the origin misattributed to a while the value is
 * b. Must remain a FAIL detection.
 */
#include <stdlib.h>
static int first(int *a) { return *a; }
static int *pick(int *a, int *b) { return (first(a), b); }  /* value is b */
int main(void) {
    int *x = malloc(sizeof(int));
    int *y = malloc(sizeof(int));
    if (!x || !y) return 1;
    int *q = pick(x, y);
    free(y);       /* destroys the object q actually is */
    *q = 1;        /* temporal violation */
    free(x);
    return 0;
}

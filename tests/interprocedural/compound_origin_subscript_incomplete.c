/*
 * Incident #64 regression (value-read form): returning p[i] on a T**
 * parameter reads a pointer VALUE out of the parameter's storage; the
 * pre-fix summary claimed a borrow of the parameter's pointee. Must never
 * PASS.
 */
#include <stdlib.h>
static int *getcell(int **p, int i) { return p[i]; }
int main(void) {
    int **cells = malloc(2 * sizeof(int *));
    int *z = malloc(sizeof(int));
    if (!cells || !z) return 1;
    cells[0] = z;
    int *q = getcell(cells, 0);   /* q == z (pointer value read) */
    free(z);                      /* true origin destroyed */
    *q = 1;                       /* temporal violation */
    free(cells);
    return 0;
}

/* #41 Gate B: C4 -- bare DeclRefExpr null-check of a maybe-produced
 * destination refines MaybeNull to Owner. */
#include <stdlib.h>
extern int po_maybe(int **out);
int main(void) {
    int *out = NULL;
    int r = po_maybe(&out);
    if (out) {
        *out = 1;
        free(out);
    }
    return r;
}

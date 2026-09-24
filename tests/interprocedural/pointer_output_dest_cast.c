/* #41 Gate B: cast around the address-of breaks the exact
 * AddrOf(DeclRefExpr) shape. */
#include <stdlib.h>
extern int po_always(int **out);
int main(void) {
    int *out = NULL;
    po_always((int **)&out);
    *out = 1;
    free(out);
    return 0;
}

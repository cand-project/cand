/* #41 Gate B: nested address-of (&*out) -- not a bare DeclRefExpr
 * destination. */
#include <stdlib.h>
extern int po_always(int **out);
static void wrapper(int **out) {
    po_always(&*out);
}
int main(void) {
    int *p = NULL;
    wrapper(&p);
    free(p);
    return 0;
}

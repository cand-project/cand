/* #41 Gate B [R6]: ParmVarDecl destination -- a parameter is not
 * function-local storage; the call keeps today's obligation. */
#include <stdlib.h>
extern int po_always(int **out);
static void wrapper(int **out) {
    po_always(out);
    **out = 1;
}
int main(void) {
    int *p = NULL;
    wrapper(&p);
    free(p);
    return 0;
}

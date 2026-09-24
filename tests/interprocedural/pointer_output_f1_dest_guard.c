/* #41 Gate B [F1]: the assigned result is not guarded; the bare
 * destination DeclRefExpr in the condition is C4. */
#include <stdlib.h>
extern void *po_status_ptr(int **out);
int main(void) {
    int *out = NULL;
    void *r = po_status_ptr(&out);
    if (out) {
        *out = 1;
        free(out);
    }
    return r != 0;
}

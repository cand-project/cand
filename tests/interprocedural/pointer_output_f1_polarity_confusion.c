/* #41 Gate B [F1]: `if (f(&out))` on a success:zero pointer-returning
 * callee is the FAILURE edge (truthiness == `!= 0`); a call expression
 * is never interpreted as a C4 destination null-check, so the deref on
 * this branch is unrefined. */
#include <stdlib.h>
extern void *po_status_zero_ptr(int **out);
int main(void) {
    int *out = NULL;
    if (po_status_zero_ptr(&out)) {
        return *out;
    }
    return 0;
}

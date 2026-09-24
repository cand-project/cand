/* #41 Gate B [F1]: `if (f(&out))` on a pointer-returning produces
 * callee refines by RETURN polarity only (success: nonzero). */
#include <stdlib.h>
extern void *po_status_ptr(int **out);
int main(void) {
    int *out = NULL;
    if (po_status_ptr(&out)) {
        *out = 1;
        free(out);
    }
    return 0;
}

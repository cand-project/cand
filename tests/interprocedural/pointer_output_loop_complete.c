/* #41 Gate B [R9]: loop-carried produce -- even a loop that fully
 * consumes the produced object each iteration joins the back edge into a
 * MaybeNull (null-initialized) or Unknown (absent) pre-state at the
 * produce, so the call is refused. Fail-closed by design. */
#include <stdlib.h>
extern int po_always(int **out);
int main(void) {
    int *out;
    for (int i = 0; i < 3; i++) {
        po_always(&out);
        *out = 1;
        free(out);
    }
    return 0;
}

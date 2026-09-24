/* #41 Gate B: C1 in a while condition; the body consumes the produced
 * object each iteration, so the back-edge slot state is Moved (an
 * allowed produce pre-state). */
#include <stdlib.h>
extern int po_nonzero(int **out);
int main(void) {
    int *out = NULL;
    while (po_nonzero(&out)) {
        *out = 1;
        free(out);
    }
    return 0;
}

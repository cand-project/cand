/* #41 Gate B: !!-parity normalization -- `!!x` is `x`. */
#include <stdlib.h>
extern int po_nonzero(int **out);
int main(void) {
    int *out = NULL;
    if (!!(po_nonzero(&out))) {
        *out = 1;
        free(out);
    }
    return 0;
}

/* #41 Gate B: C1 truthiness -- `if (call)` is `call != 0`, refining
 * under success: nonzero. */
#include <stdlib.h>
extern int po_nonzero(int **out);
int main(void) {
    int *out = NULL;
    if (po_nonzero(&out)) {
        *out = 1;
        free(out);
    }
    return 0;
}

/* #41 Gate B: C3 truthiness -- `if (r)` is `r != 0`. */
#include <stdlib.h>
extern int po_nonzero(int **out);
int main(void) {
    int *out = NULL;
    int r = po_nonzero(&out);
    if (r) {
        *out = 1;
        free(out);
    }
    return r;
}

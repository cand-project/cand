/* #41 Gate B: C2 truthiness -- `if ((r = call))` is `call != 0`. */
#include <stdlib.h>
extern int po_nonzero(int **out);
int main(void) {
    int *out = NULL;
    int r;
    if ((r = po_nonzero(&out))) {
        *out = 1;
        free(out);
    }
    return r;
}

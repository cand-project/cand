/* #41 Gate B: C3 -- assigned-result guard via the pending map. */
#include <stdlib.h>
extern int po_zero(int **out);
int main(void) {
    int *out = NULL;
    int r = po_zero(&out);
    if (r == 0) {
        *out = 1;
        free(out);
    }
    return r;
}

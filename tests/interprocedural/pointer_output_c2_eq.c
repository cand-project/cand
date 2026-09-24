/* #41 Gate B: C2 -- assignment embedded in the condition. */
#include <stdlib.h>
extern int po_zero(int **out);
int main(void) {
    int *out = NULL;
    int r;
    if ((r = po_zero(&out)) == 0) {
        *out = 1;
        free(out);
    }
    return r;
}

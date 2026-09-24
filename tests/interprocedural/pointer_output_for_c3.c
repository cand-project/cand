/* #41 Gate B: C3 in a for condition guards the body edge. */
#include <stdlib.h>
extern int po_zero(int **out);
int main(void) {
    int *out = NULL;
    int r = po_zero(&out);
    for (; r == 0; ) {
        *out = 1;
        free(out);
        break;
    }
    return r;
}

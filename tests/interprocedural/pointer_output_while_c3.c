/* #41 Gate B: C3 in a while condition. The produce happens before the
 * loop and the body breaks out, so no back edge joins the loop header;
 * the pending guard refines the body edge. */
#include <stdlib.h>
extern int po_zero(int **out);
int main(void) {
    int *out = NULL;
    int r = po_zero(&out);
    while (r == 0) {
        *out = 1;
        free(out);
        break;
    }
    return r;
}

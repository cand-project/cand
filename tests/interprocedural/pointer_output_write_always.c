/* #41 Gate B: write:always converts without any guard; the destination
 * may be completely unbound before the call (absent pre-state). */
#include <stdlib.h>
extern int po_always(int **out);
int main(void) {
    int *out;
    po_always(&out);
    *out = 1;
    free(out);
    return 0;
}

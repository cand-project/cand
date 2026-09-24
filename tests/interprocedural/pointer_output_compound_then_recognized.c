/* #41 Gate B [F3]: an unrecognized compound condition neither refines
 * nor kills a pending guard; a LATER recognized single form refines. */
#include <stdlib.h>
extern int po_zero(int **out);
int main(void) {
    int *out = NULL;
    int r = po_zero(&out);
    if (r < 0 && out != NULL) {
        return 1;
    }
    if (r == 0) {
        *out = 1;
        free(out);
    }
    return r;
}

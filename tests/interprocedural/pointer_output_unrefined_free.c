/* #41 Gate B [R1/F8]: unrefined free of a maybe-produced binding emits
 * unrefined-out-owner-use -- NOT the silent free(NULL) no-op. */
#include <stdlib.h>
extern int po_maybe(int **out);
int main(void) {
    int *out = NULL;
    po_maybe(&out);
    free(out);
    return 0;
}

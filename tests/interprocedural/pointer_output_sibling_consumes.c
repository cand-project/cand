/* #41 Gate B [2.2.5]: sibling parameters of an accepted produces call
 * keep the existing contract trust model -- param 1 consumes, so the
 * later free is a use-after-free (FAIL). */
#include <stdlib.h>
extern int po_pair(int **out, int *owned);
int main(void) {
    int *owned = malloc(8);
    int *out = NULL;
    if (po_pair(&out, owned) == 0) {
        *out = 1;
        free(out);
    }
    free(owned);
    return 0;
}

/* #41 Gate B: write:on_success requires a null-initialized
 * destination; a live malloc'd binding fails the pre-state check. */
#include <stdlib.h>
extern int po_zero(int **out);
int main(void) {
    int *out = malloc(8);
    if (po_zero(&out) == 0) {
        *out = 1;
        free(out);
    } else {
        free(out);
    }
    return 0;
}

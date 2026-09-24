/* #41 Gate B [2.2.7]: contract-body-conflict -- a symbol with a
 * visible same-TU definition never receives the produces effect; the
 * output must be identical to a run without the contract. */
#include <stddef.h>
static int po_visible(int **out) {
    (void)out;
    return 0;
}
int main(void) {
    int *out = NULL;
    if (po_visible(&out) == 0) {
        *out = 1;
    }
    return 0;
}

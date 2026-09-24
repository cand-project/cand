/* #41 Gate B [2.2.6]: K&R-style definition -- refused (and the visible
 * body independently refuses the effect); output identical to a run
 * without the contract. */
#include <stddef.h>
static int po_knr(out) int **out; {
    (void)out;
    return 0;
}
int main(void) {
    int *out = NULL;
    if (po_knr(&out) == 0) {
        *out = 1;
    }
    return 0;
}

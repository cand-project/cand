/* #41 Gate B: guard after an intervening call -- the destination value
 * appears in another (unknown) call between the produce and the guard,
 * so the pending guard is killed and the later deref is unrefined. */
#include <stdlib.h>
extern int po_maybe(int **out);
extern void opaque(int *value);
int main(void) {
    int *out = NULL;
    int r = po_maybe(&out);
    opaque(out);
    if (r == 0) {
        *out = 1;
    }
    return 0;
}

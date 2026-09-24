/* #41 Gate B [2.2.3/R9]: a produce inside a do/while body sees the
 * back-edge-joined pre-state (MaybeNull) and is refused, even though the
 * C4 null-check fully consumes the object each iteration. */
#include <stdlib.h>
extern int po_always_maybe(int **out);
int main(void) {
    int *out = NULL;
    int n = 2;
    do {
        po_always_maybe(&out);
        if (out) {
            *out = 1;
            free(out);
        }
    } while (--n);
    return 0;
}

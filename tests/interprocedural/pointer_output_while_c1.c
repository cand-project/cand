/* #41 Gate B [2.2.3/R9]: a produce in a loop condition sees the
 * back-edge-joined pre-state (Null joined with the previous iteration's
 * binding is MaybeNull) -- not in {absent, Null, Moved/MaybeMoved} -- so
 * the call keeps today's obligation even though each iteration fully
 * consumes the object. */
#include <stdlib.h>
extern int po_nonzero(int **out);
int main(void) {
    int *out = NULL;
    while (po_nonzero(&out)) {
        *out = 1;
        free(out);
    }
    return 0;
}

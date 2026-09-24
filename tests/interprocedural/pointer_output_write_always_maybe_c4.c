/* #41 Gate B: write:always + nullable:true binds {fresh, MaybeNull};
 * a C4 null-check on the destination refines it to Owner. */
#include <stdlib.h>
extern int po_always_maybe(int **out);
int main(void) {
    int *out = NULL;
    po_always_maybe(&out);
    if (out) {
        *out = 1;
        free(out);
    }
    return 0;
}

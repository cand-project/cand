/* #41 Gate B [R1]: unrefined deref of a maybe-produced binding
 * (write:always + nullable:true, no guard) emits
 * unrefined-out-owner-use -- INCOMPLETE, not a null-safety FAIL. */
#include <stdlib.h>
extern int po_always_maybe(int **out);
int main(void) {
    int *out = NULL;
    po_always_maybe(&out);
    return *out;
}

/* #41 Gate B [R8]: destination pre-state is a live Owner -- not in
 * {absent, Null, Moved/MaybeMoved}. */
#include <stdlib.h>
extern int po_always(int **out);
int main(void) {
    int *out = malloc(8);
    po_always(&out);
    *out = 1;
    free(out);
    return 0;
}

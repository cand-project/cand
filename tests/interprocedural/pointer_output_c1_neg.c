/* #41 Gate B: C1 negated truthiness -- `if (!call)` is `call == 0`,
 * refining under success: zero. */
#include <stdlib.h>
extern int po_zero(int **out);
int main(void) {
    int *out = NULL;
    if (!po_zero(&out)) {
        *out = 1;
        free(out);
    }
    return 0;
}

/* #41 Gate B: variadic callee -- refused even though the out argument
 * occupies a fixed parameter slot. */
#include <stdlib.h>
extern int po_variadic(int **out, ...);
int main(void) {
    int *out = NULL;
    po_variadic(&out, 1);
    *out = 1;
    free(out);
    return 0;
}

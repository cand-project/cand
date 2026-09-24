/* #41 Gate B [2.2.1]: the declared parameter type is pointer to
 * double pointer (T ***) -- not a single-level out slot; the bundle is
 * rejected at application time (invalid trusted contract). */
#include <stddef.h>
extern int po_triple(int ***slot);
int main(void) {
    int **slot = NULL;
    po_triple(&slot);
    return 0;
}

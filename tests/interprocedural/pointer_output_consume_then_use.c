/* #41 Gate B: use-after-free of the produced object -- temporal defect,
 * FAIL. */
#include <stdlib.h>
extern int po_always(int **out);
int main(void) {
    int *out = NULL;
    po_always(&out);
    free(out);
    return *out;
}

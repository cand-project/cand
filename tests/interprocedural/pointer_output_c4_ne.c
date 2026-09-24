/* #41 Gate B: C4 explicit form -- `if (dest != NULL)`. */
#include <stdlib.h>
extern int po_maybe(int **out);
int main(void) {
    int *out = NULL;
    int r = po_maybe(&out);
    if (out != NULL) {
        *out = 1;
        free(out);
    }
    return r;
}

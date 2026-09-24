/* #41 Gate B: fully parenthesized condition forms still match. */
#include <stdlib.h>
extern int po_zero(int **out);
int main(void) {
    int *out = NULL;
    if (((po_zero(&out))) == 0) {
        *out = 1;
        free(out);
    }
    return 0;
}

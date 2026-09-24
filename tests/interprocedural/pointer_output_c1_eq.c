/* #41 Gate B: C1 -- call directly in an if condition, == constant
 * matching the success polarity (success: zero). */
#include <stdlib.h>
extern int po_zero(int **out);
int main(void) {
    int *out = NULL;
    if (po_zero(&out) == 0) {
        *out = 1;
        free(out);
    }
    return 0;
}

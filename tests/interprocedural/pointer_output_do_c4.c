/* #41 Gate B: C4 in a do/while body; each iteration fully consumes the
 * produced object before the back edge. */
#include <stdlib.h>
extern int po_always_maybe(int **out);
int main(void) {
    int *out = NULL;
    int n = 2;
    do {
        po_always_maybe(&out);
        if (out) {
            *out = 1;
            free(out);
        }
    } while (--n);
    return 0;
}

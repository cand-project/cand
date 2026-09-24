/* #41 Gate B [R9]: a loop that fully consumes the produced object each
 * iteration (Moved pre-state at the second produce) stays converted. */
#include <stdlib.h>
extern int po_always(int **out);
int main(void) {
    int *out;
    for (int i = 0; i < 3; i++) {
        po_always(&out);
        *out = 1;
        free(out);
    }
    return 0;
}

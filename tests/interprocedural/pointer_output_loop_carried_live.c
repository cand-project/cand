/* #41 Gate B [R9]: loop-carried produce -- the second iteration's call
 * sees a live binding in the destination slot and is refused. */
#include <stdlib.h>
extern int po_always(int **out);
int main(void) {
    int *out = NULL;
    int n = 2;
    while (n--) {
        po_always(&out);
    }
    *out = 1;
    return 0;
}

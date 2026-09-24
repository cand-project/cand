/* #41 Gate B [R3b]: the destination address escapes through another
 * call; alias joins are refused. */
#include <stdlib.h>
extern void escape(int **slot);
extern int po_always(int **out);
int main(void) {
    int *out = NULL;
    escape(&out);
    po_always(&out);
    *out = 1;
    free(out);
    return 0;
}

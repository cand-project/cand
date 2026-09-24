/* #41 Gate B: global storage destination. */
#include <stdlib.h>
extern int po_always(int **out);
static int *g_out;
int main(void) {
    po_always(&g_out);
    *g_out = 1;
    free(g_out);
    return 0;
}

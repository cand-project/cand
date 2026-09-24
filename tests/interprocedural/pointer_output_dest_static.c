/* #41 Gate B: static local storage destination (no local storage). */
#include <stdlib.h>
extern int po_always(int **out);
int main(void) {
    static int *s_out;
    po_always(&s_out);
    *s_out = 1;
    free(s_out);
    return 0;
}

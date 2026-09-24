/* #41 Gate B: struct-field destination. */
#include <stdlib.h>
extern int po_always(int **out);
struct wrapper { int *field; };
int main(void) {
    struct wrapper w;
    w.field = NULL;
    po_always(&w.field);
    *w.field = 1;
    free(w.field);
    return 0;
}

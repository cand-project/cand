/* #41 Gate B: overwriting a live produced owner keeps today's
 * tracked-owner-overwrite obligation (ordinary-lattice consistency). */
#include <stdlib.h>
extern int po_always(int **out);
int main(void) {
    int *out = NULL;
    po_always(&out);
    out = malloc(8);
    *out = 1;
    free(out);
    return 0;
}

/* #41 Gate B: a dropped produced object is silent, exactly like a
 * dropped owned return -- the produced object flows through the
 * ordinary ownership lattice and leaks are not a cand1 claim. */
#include <stdlib.h>
extern int po_always(int **out);
int main(void) {
    int *out = NULL;
    po_always(&out);
    *out = 1;
    return 0;
}

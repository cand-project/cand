#include <stdlib.h>

/*
 * P0.2 (spec §12): the loop body mutates the pointee, not the ownership
 * state. The fixed point stays Owned and the single free is safe.
 *
 * Expected: PASS.
 */
int main(void)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    *p = 0;
    for (int i = 0; i < 10; ++i) {
        *p += i;
    }

    free(p);
    return 0;
}

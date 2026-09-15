#include <stdlib.h>

/*
 * P0.2 (spec §12): the loop body can execute more than once, so the free is
 * reached again with the object already destroyed. The join of the back
 * edge yields MaybeDead.
 *
 * Expected: FAIL CAND-T003 (possible).
 */
int f(int cond)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    while (cond) {
        free(p);
    }

    return 0;
}

int main(void)
{
    return f(1);
}

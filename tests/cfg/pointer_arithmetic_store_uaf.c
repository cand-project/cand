#include <stdlib.h>

/*
 * Independent-review regression: store through a pointer-arithmetic base
 * after destruction.
 *
 * Expected: FAIL CAND-T002, ASan violation.
 */
int main(void)
{
    int *p = malloc(4 * sizeof *p);
    if (p == NULL) {
        return 2;
    }
    free(p);
    *(p + 2) = 7;
    return 0;
}

#include <stdlib.h>

/*
 * Independent-review regression: a dereference whose base is pointer
 * arithmetic must still be checked against the tracked object. Before this
 * fix, `*(p + 1)` after free returned PASS (ASan: heap-use-after-free).
 *
 * Expected: FAIL CAND-T002, ASan violation.
 */
int main(void)
{
    int *p = malloc(4 * sizeof *p);
    if (p == NULL) {
        return 2;
    }
    p[0] = 1;
    free(p);
    return *(p + 1);
}

#include <stdlib.h>

/*
 * Independent-review regression: the operand of sizeof is unevaluated and
 * must not be treated as an access. Before this fix, `sizeof *p` after free
 * produced a definite CAND-T002 (a false FAIL).
 *
 * Expected: PASS, ASan clean.
 */
int main(void)
{
    int *p = malloc(4 * sizeof *p);
    if (p == NULL) {
        return 2;
    }
    int n = 0;
    free(p);
    n = sizeof *p;
    return n == 4 ? 0 : 1;
}

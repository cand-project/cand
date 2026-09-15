#include <stdlib.h>

/*
 * Only one branch of a conditional expression executes, so the linear P0
 * analyzer must not flatten branch state transitions. Before this fixture
 * existed, `c ? free(p) : free(p)` produced a spurious CAND-T003.
 *
 * Required P0.1 result: INCOMPLETE (conditional-expression), no finding.
 */
int main(void)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    int c = 1;
    c ? free(p) : free(p);

    return 0;
}

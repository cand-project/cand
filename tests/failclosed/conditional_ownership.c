#include <stdlib.h>

/*
 * P0.2 improvement: conditional evaluation is now modeled over the CFG, so
 * only one branch's ownership transition is applied per path. In P0.1 this
 * was INCOMPLETE (conditional-expression); the CFG proves that each path
 * destroys the object exactly once and no access follows the join.
 *
 * Expected: PASS.
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

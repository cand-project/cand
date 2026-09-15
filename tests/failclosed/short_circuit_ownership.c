#include <stdlib.h>

/*
 * P0.2: short-circuit evaluation is modeled over the CFG. On the path where
 * `c` is true, `free(p)` runs in the RHS block and the later `free(p)` is a
 * second destruction of the same object. P0.1 reported this as INCOMPLETE
 * (short-circuit-expression); the CFG establishes a possible double
 * destruction.
 *
 * Expected: FAIL CAND-T003 (certainty "possible").
 */
int main(void)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    int c = 0;
    int ok = c && (free(p), 1);
    (void)ok;

    free(p);
    return 0;
}

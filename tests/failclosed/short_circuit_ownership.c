#include <stdlib.h>

/*
 * The right-hand side of && / || is conditionally evaluated and must not
 * drive linear ownership-state transitions.
 *
 * Required P0.1 result: INCOMPLETE (short-circuit-expression).
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

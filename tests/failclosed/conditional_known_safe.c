#include <stdlib.h>

/*
 * Regression guard against over-triggering the conditional-expression rule:
 * a conditional that contains no ownership-affecting operation (no calls, no
 * tracked objects) is ordinary arithmetic and must not degrade a fully
 * modeled lifecycle to INCOMPLETE.
 *
 * Required P0.1 result: PASS.
 */
int main(void)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    *p = 42;
    int result = *p;
    free(p);

    return result == 42 ? 0 : 1;
}

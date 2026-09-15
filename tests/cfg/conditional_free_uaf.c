#include <stdlib.h>

/*
 * P0.2 (spec §9B): a path exists (flag != 0) on which the object is
 * destroyed before the access. This is a possible use-after-free, not an
 * unresolved obligation.
 *
 * Expected: FAIL CAND-T002 (certainty possibly "possible").
 */
int f(int flag)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    if (flag) {
        free(p);
    }

    return *p;
}

int main(void)
{
    return f(1);
}

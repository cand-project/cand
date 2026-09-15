#include <stdlib.h>

/*
 * P0.2 final-state semantics: the object is destroyed on one path and
 * released to NULL on the other. After the join the storage is MaybeDead, so
 * the following free is a possible double destruction. main exercises the
 * destroying path, so ASan confirms it.
 *
 * Expected: FAIL CAND-T003 (certainty possible), ASan violation.
 */
int f(int c)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    if (c) {
        free(p);
    } else {
        p = 0;
    }

    free(p);
    return 0;
}

int main(void)
{
    return f(1);
}

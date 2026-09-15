#include <stdlib.h>

/*
 * P0.2 (spec §9A): the free in the early-return branch is unreachable from
 * the path that reaches `*p = 42`, so the later access is safe.
 * A source-order walker cannot establish this.
 *
 * Expected: PASS.
 */
int f(int error)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    if (error) {
        free(p);
        return 1;
    }

    *p = 42;
    free(p);
    return 0;
}

int main(void)
{
    return f(0);
}

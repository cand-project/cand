#include <stdlib.h>

/*
 * P0.2 (spec §9F): the destroying branch returns, so the continuation only
 * executes on paths where the object is still owned.
 *
 * Expected: PASS.
 */
int f(int flag)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    if (flag) {
        free(p);
        return 0;
    }

    *p = 4;
    free(p);
    return 0;
}

int main(void)
{
    return f(1);
}

#include <stdlib.h>

/*
 * P0.2 (spec §9D): every path destroys the object exactly once and no
 * access follows the join.
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
    } else {
        free(p);
    }

    return 0;
}

int main(void)
{
    return f(1);
}

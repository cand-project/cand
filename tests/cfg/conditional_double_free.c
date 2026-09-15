#include <stdlib.h>

/*
 * P0.2 (spec §9C): on the path where flag != 0, the object is destroyed
 * twice.
 *
 * Expected: FAIL CAND-T003.
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

    free(p);
    return 0;
}

int main(void)
{
    return f(1);
}

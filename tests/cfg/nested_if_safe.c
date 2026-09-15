#include <stdlib.h>

/*
 * P0.2 (spec §22): nested control flow without ownership errors.
 *
 * Expected: PASS.
 */
int f(int a, int b)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    if (a) {
        if (b) {
            *p = 1;
            free(p);
            return 0;
        }
        *p = 2;
    } else {
        *p = 3;
    }

    free(p);
    return 0;
}

int main(void)
{
    return f(1, 1);
}

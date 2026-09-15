#include <stdlib.h>

/*
 * P0.2 (spec §22): destruction happens inside the inner then-branch, and the
 * outer else-path also reaches the access.
 *
 * Expected: FAIL CAND-T002.
 */
int f(int a, int b)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    if (a) {
        if (b) {
            free(p);
        }
    }

    return *p;
}

int main(void)
{
    return f(1, 1);
}

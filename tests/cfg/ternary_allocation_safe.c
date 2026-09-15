#include <stdlib.h>

/*
 * P0.2 precision: an initializer that yields a fresh allocation or NULL on
 * every path (`c ? malloc(a) : malloc(b)`) binds as an owned storage, so the
 * later free is understood instead of becoming free-untracked-pointer.
 *
 * Expected: PASS (ASan clean).
 */
int f(int c)
{
    int *p = c ? malloc(4) : malloc(8);
    if (p == NULL) {
        return 2;
    }
    *p = 1;
    free(p);
    return 0;
}

int main(void)
{
    return f(1);
}

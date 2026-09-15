#include <stdlib.h>

/*
 * Independent-review regression: the idiomatic free-then-reallocate-reuse
 * pattern uses `sizeof *p` after the free. It is safe and must be PASS, not
 * a spurious use-after-free.
 *
 * Expected: PASS, ASan clean.
 */
int main(void)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }
    free(p);

    p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }
    *p = 9;
    free(p);
    return 0;
}

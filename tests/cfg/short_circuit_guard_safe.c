#include <stdlib.h>

/*
 * P0.2 precision: the short-circuit guard frees and returns on the same
 * path, so the later access only executes when the object is still owned.
 * P0.1 reported this as INCOMPLETE (short-circuit-expression).
 *
 * Expected: PASS (ASan clean).
 */
int f(int c)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }
    *p = 1;

    if (c && (free(p), 1)) {
        return 9;
    }
    return *p;
}

int main(void)
{
    return f(1);
}

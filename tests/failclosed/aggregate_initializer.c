#include <stdlib.h>

struct S {
    int *p;
};

/*
 * The assignment form `s.p = malloc(...)` is reported as
 * allocation-to-untracked-storage. The aggregate-initializer form must not
 * be silently accepted, or two spellings of the same unmodelled storage
 * location would get different results.
 *
 * Required P0.1 result: INCOMPLETE (allocation-to-untracked-storage).
 */
int main(void)
{
    struct S s = { .p = malloc(sizeof(int)) };

    *s.p = 1;
    return *s.p;
}

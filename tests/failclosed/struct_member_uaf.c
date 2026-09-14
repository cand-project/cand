#include <stdlib.h>

struct S {
    int *p;
};

/*
 * BLOCKER regression (P0.1): the allocation is stored in a struct member,
 * which P0 cannot track. Before P0.1 this returned PASS while AddressSanitizer
 * reports a heap-use-after-free on `return *s.p`.
 *
 * Required P0.1 result: INCOMPLETE (never PASS). Precise member tracking is
 * a later phase.
 */
int main(void)
{
    struct S s;

    s.p = malloc(sizeof *s.p);
    *s.p = 42;

    free(s.p);

    return *s.p;
}

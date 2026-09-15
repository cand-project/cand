#include <stdlib.h>

static int *make_value(void)
{
    return malloc(sizeof(int));
}

/*
 * BLOCKER regression (P0.1): ownership of the pointer returned by
 * `make_value` is unknown to P0 (no interprocedural summary, no trusted
 * contract). Before P0.1 this returned PASS while AddressSanitizer reports a
 * heap-use-after-free on `return *p`.
 *
 * Required P0.1 result: INCOMPLETE (never PASS).
 */
int main(void)
{
    int *p = make_value();

    *p = 42;
    free(p);

    return *p;
}

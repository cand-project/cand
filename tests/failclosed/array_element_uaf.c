#include <stdlib.h>

/*
 * BLOCKER regression (P0.1): the allocation is stored in an array element,
 * which P0 cannot track. Before P0.1 this returned PASS while AddressSanitizer
 * reports a heap-use-after-free on `return *items[0]`.
 *
 * Required P0.1 result: INCOMPLETE (never PASS).
 */
int main(void)
{
    int *items[1];

    items[0] = malloc(sizeof *items[0]);
    *items[0] = 42;

    free(items[0]);

    return *items[0];
}

#include <stdlib.h>

/*
 * Inline assembly is opaque to the analyzer and can affect any ownership or
 * lifetime state.
 *
 * Required P0.1 result: INCOMPLETE (inline-asm).
 */
int main(void)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    __asm__ volatile("" ::: "memory");

    free(p);
    return 0;
}

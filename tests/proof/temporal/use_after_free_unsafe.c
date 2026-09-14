#include <stdlib.h>

int main(void)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    *p = 7;
    free(p);

    /*
     * Ordinary C compilers accept this under the repository warning profile,
     * but AddressSanitizer must report heap-use-after-free when executed.
     */
    volatile int observed = *p;
    (void)observed;
    return 0;
}

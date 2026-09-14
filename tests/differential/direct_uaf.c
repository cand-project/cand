#include <stdlib.h>

int main(void)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    *p = 7;
    free(p);

    /* Ordinary C compilers accept this; ASan reports heap-use-after-free. */
    volatile int observed = *p;
    (void)observed;
    return 0;
}

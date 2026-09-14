#include <stdlib.h>

int main(void)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    *p = 7;
    volatile int observed = *p;
    (void)observed;
    free(p);
    return 0;
}

#include <stdlib.h>

int main(void)
{
    int *items[1];

    items[0] = malloc(sizeof *items[0]);
    *items[0] = 42;

    free(items[0]);

    /* ASan: heap-use-after-free. P0.1: must not return PASS. */
    return *items[0];
}

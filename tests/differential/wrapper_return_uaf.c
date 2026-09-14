#include <stdlib.h>

static int *make_value(void)
{
    return malloc(sizeof(int));
}

int main(void)
{
    int *p = make_value();

    *p = 42;
    free(p);

    /* ASan: heap-use-after-free. P0.1: must not return PASS. */
    return *p;
}

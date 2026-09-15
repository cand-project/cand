#include <stdlib.h>

struct S {
    int *p;
};

int main(void)
{
    struct S s;

    s.p = malloc(sizeof *s.p);
    *s.p = 42;

    free(s.p);

    /* ASan: heap-use-after-free. P0.1: must not return PASS. */
    return *s.p;
}

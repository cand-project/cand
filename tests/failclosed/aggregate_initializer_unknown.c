#include <stdlib.h>

struct S {
    char *p;
};

static char *make(void)
{
    return malloc(4);
}

/*
 * An owned pointer-returning call stored through an aggregate initializer is
 * outside the tracked storage model and must fail closed.
 *
 * Required P0.1 result: INCOMPLETE (unknown-pointer-return-ownership).
 */
int main(void)
{
    struct S s = { .p = make() };

    return s.p[0];
}

#include <stdlib.h>

struct S {
    char *p;
};

static char *make(void)
{
    return malloc(4);
}

/*
 * An unmodelled pointer-returning call stored through an aggregate
 * initializer must fail closed exactly like the assignment form.
 *
 * Required P0.1 result: INCOMPLETE (unknown-pointer-return-ownership).
 */
int main(void)
{
    struct S s = { .p = make() };

    return s.p[0];
}

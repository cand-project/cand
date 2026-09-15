/*
 * Stack escapes routed through a pointer *variable* (not just an immediate
 * `return &x`) must be detected, including when the address is taken of a
 * member of a local object.
 *
 * Required P0.1 result: INCOMPLETE (stack-pointer-return).
 */
struct S {
    int x;
};

int *via_variable(void)
{
    struct S s;
    s.x = 1;
    return &s.x;
}

int main(void)
{
    int *p = via_variable();
    return *p;
}

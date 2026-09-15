#include <stdlib.h>
struct S { int *p; };
int main(void) {
    struct S a = {0};
    struct S b = {0};
    a.p = malloc(sizeof *a.p);
    *a.p = 42;
    b = a;
    free(a.p);
    return *b.p;
}

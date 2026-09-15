#include <stdlib.h>
struct S { int *p; };
int main(void) {
    int *p = malloc(sizeof *p);
    *p = 7;
    struct S b = { .p = p };
    free(p);
    return *b.p;
}

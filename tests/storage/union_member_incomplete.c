#include <stdlib.h>
union U { int *p; int *q; };
int main(void) {
    union U u = {0};
    u.p = malloc(sizeof *u.p);
    *u.p = 3;
    free(u.p);
    return *u.q;
}

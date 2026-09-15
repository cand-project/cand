#include <stdatomic.h>
#include <stdlib.h>
int main(void) {
    int *p = malloc(sizeof *p);
    *p = 1;
    _Atomic(int *) slot = p;
    free(p);
    int *q = atomic_load(&slot);
    return *q;
}

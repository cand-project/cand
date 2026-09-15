#include <stdint.h>
#include <stdlib.h>
int main(void) {
    int *p = malloc(sizeof *p);
    uintptr_t raw = (uintptr_t)p;
    free(p);
    return *(int *)raw;
}

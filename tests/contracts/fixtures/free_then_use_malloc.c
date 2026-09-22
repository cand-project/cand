#include <stdlib.h>

/*
 * Allocator-core adversarial fixture (free, contracts/libc.yaml):
 * use-after-free must FAIL with the reviewed contracts active; the
 * destroys-effect on parameter 0 must not weaken the post-free read.
 */
int run(void) {
    char *p = malloc(8);
    if (!p) return 1;
    free(p);
    return p[0];
}

int main(void) { return run(); }

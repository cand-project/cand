#include <stdlib.h>

/*
 * Incident #62 regression (callee-mapping form): the borrow origin returned
 * by a decided callee is mapped through the argument NAME at the call site.
 * When that argument parameter is reassigned in the body, the mapping
 * identifies the wrong object and the pre-fix verdict was PASS on a real
 * use-after-free. The mapped origin must fail closed to Unknown: INCOMPLETE.
 */

static void use(int *p) { (void)*p; }
static void destroy(int *p) { free(p); }

static int *helper(int *x) { return x; }

static int *reassign_helper(int *p, int *r) { p = r; return helper(p); }

int run(void) {
    int *a = malloc(sizeof(int));
    int *b = malloc(sizeof(int));
    if (!a || !b) return 1;
    int *q = reassign_helper(a, b);
    destroy(b);
    use(q); /* temporal violation: use after destroy */
    destroy(a);
    return 0;
}

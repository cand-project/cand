#include <stdlib.h>

/*
 * Incident #62 control: correct attribution must survive the fix. Here the
 * parameter is reassigned but the function returns the SOURCE parameter
 * directly, so the borrow origin (arg 1) is sound and the caller-side
 * invalidation must catch the use after destroy: FAIL, not INCOMPLETE and
 * not PASS. Guards against over-collapsing correct origins.
 */

static void use(int *p) { (void)*p; }
static void destroy(int *p) { free(p); }

static int *reassign_return_source(int *p, int *r) { p = r; return r; }

int run(void) {
    int *a = malloc(sizeof(int));
    int *b = malloc(sizeof(int));
    if (!a || !b) return 1;
    int *q = reassign_return_source(a, b);
    destroy(b);
    use(q); /* detected: q borrows b, which was destroyed */
    destroy(a);
    return 0;
}

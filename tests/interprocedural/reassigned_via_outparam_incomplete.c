#include <stdlib.h>

/*
 * Incident #62 boundary probe: reassignment of a parameter through a
 * pointer-output helper (no syntactic assignment to `p` in f()).
 *
 * The ADR-0025 guard cannot see the write through `*out`, so the summary
 * path alone would still resolve `return p` to borrow_from_arg@0. This
 * shape stays fail-closed through the escape analysis: `&p` is a tracked
 * pointer passed to a callee, so the call emits obligations and the verdict
 * is INCOMPLETE, never PASS. This fixture pins that boundary.
 */
static void use(int *p) { *p = 42; }
static void destroy(int *p) { free(p); }
static void set(int **out, int *v) { *out = v; }
static int *f(int *p, int *r) { set(&p, r); return p; }
int main(void) {
    int *a = malloc(sizeof(int));
    int *b = malloc(sizeof(int));
    int *q = f(a, b);
    destroy(b);        /* destroys the object q actually borrows */
    use(q);            /* temporal violation */
    destroy(a);
    return 0;
}

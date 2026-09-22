#include <stdlib.h>
#include <string.h>

/*
 * ADVERSARIAL fixture for the allocator core (contracts/libc.yaml).
 *
 * realloc's lifetime replacement is path-sensitive; the verifier keeps the
 * return unknown even though the bundle records the intended semantics
 * (consumes param 0, owned nullable return). Using the OLD pointer after a
 * realloc must therefore never PASS: the verdict here is pinned to
 * INCOMPLETE (unknown-pointer-return-ownership at the call plus a
 * contract-body-conflict obligation on the old-object use), both
 * fail-closed.
 */
int run(void) {
    char *p = malloc(8);
    if (!p) return 1;
    memset(p, 0, 8);
    char *q = realloc(p, 16);
    if (!q) { free(p); return 1; }
    return p[0]; /* use of the old (replaced) object: must be caught */
}

int main(void) { return run(); }

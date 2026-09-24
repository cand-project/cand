#include <stdlib.h>

/*
 * Milestone #73 (ADR-0031), Area C: pointer-difference delta.
 *
 * `p += (q - p)` with q a different heap pointer is the only well-
 * defined shape by which arithmetic can REBIND a cursor to another
 * object; deltas that mention a pointer value must stay fail-closed
 * (the parent is not preserved).
 *
 * Required verdicts:
 *   TODAY (pre-fix): INCOMPLETE.
 *   AFTER  (post-fix): INCOMPLETE (never becomes a PASS).
 */
static int use_rebound(const char *base, const char *q) {
    const char *p = base;
    p += (q - p); /* pointer-mentioning delta: rebind-shaped, fail-closed */
    return (unsigned char)*p;
}

int main(void) {
    char *a = malloc(32);
    char *b = malloc(32);
    if (!a || !b) { free(a); free(b); return 1; }
    int r = use_rebound(a, b);
    free(a);
    free(b);
    return r ? 0 : 2;
}

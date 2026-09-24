#include <stdlib.h>
#include <string.h>

/*
 * Milestone #73 (ADR-0031), Area R: borrowed local returned through an
 * undeclared return.
 *
 * A static helper holds the shared borrow created by memchr
 * (BorrowFromArg summary from the reviewed libc-memory bundle) in a
 * local and returns it while the helper's own summary return effect is
 * Unknown. Today the ReturnStmt scan has no local-origin propagation,
 * so the borrow escapes as CAND-B003. The Area R dataflow resolves the
 * return to BorrowFromArg(0) and the caller-side lifetime link makes
 * the safe usage decidable.
 *
 * Required verdicts:
 *   TODAY (pre-fix): FAIL (CAND-B003 borrow escapes through an
 *                    undeclared return).
 *   AFTER  (post-fix): PASS with the merged reviewed bundles.
 */
static char *find_x(char *s, size_t n) {
    char *t = memchr(s, 'x', n);
    return t;
}

int main(void) {
    char *buf = malloc(16);
    if (!buf) return 1;
    memset(buf, 'x', 16);
    char *hit = find_x(buf, 16);
    int ok = hit != NULL;
    free(buf);
    return ok ? 0 : 2;
}

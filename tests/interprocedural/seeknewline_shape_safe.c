#include <stdlib.h>
#include <string.h>

/*
 * Milestone #73 (ADR-0031), Area R: faithful copy of hiredis
 * read.c seekNewline (the read.c:168 ground-truth instance).
 *
 * The loop body uses BARE cursor variables as the memchr call
 * arguments (storageFor has no BinaryOperator case; arithmetic-
 * expression call args stay fail-closed by design) and reassigns both
 * the cursor and the length. Today the local holding the shared
 * borrow is returned while the function's own summary return effect
 * is Unknown -> CAND-B003. The Area R dataflow (memchr return ->
 * {0}; ret++/s = ret preserve; fixpoint stable) resolves the return
 * to BorrowFromArg(0).
 *
 * Required verdicts:
 *   TODAY (pre-fix): FAIL (CAND-B003 at the return).
 *   AFTER  (post-fix): PASS with the merged reviewed bundles.
 */
static const char *seekNewline(const char *s, size_t len) {
    const char *ret;

    if (len < 2)
        return NULL;
    while ((ret = memchr(s, '\r', len)) != NULL) {
        if (ret[1] == '\n') {
            break;
        }
        ret++;
        len -= (size_t)(ret - s);
        s = ret;
    }
    return ret;
}

int main(void) {
    char *buf = malloc(16);
    if (!buf) return 1;
    memset(buf, 'a', 13);
    buf[13] = '\r';
    buf[14] = '\n';
    const char *hit = seekNewline(buf, 15);
    int ok = hit == buf + 13;
    free(buf);
    return ok ? 0 : 2;
}

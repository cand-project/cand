#include <stdlib.h>
#include <string.h>

/*
 * Milestone #73 (ADR-0031), Area R: faithful copy of libevent
 * buffer.c find_eol_char (the buffer.c:1541/1542/1544 ground-truth
 * instances, three B003 findings in one function).
 *
 * CHUNK_SZ is 128; the cr/lf memchr pair uses BARE cursor call
 * arguments; the conditional returns and the `s += CHUNK_SZ` stride
 * (a pure integer delta) are the shapes the Area R dataflow resolves:
 * cr/lf <- memchr(s) -> {0}, returns lf/cr -> BorrowFromArg(0).
 *
 * Required verdicts:
 *   TODAY (pre-fix): FAIL (CAND-B003 at each pointer return).
 *   AFTER  (post-fix): PASS with the merged reviewed bundles.
 */
#define CHUNK_SZ 128

static const char *find_eol_char(const char *s, size_t len) {
    while (len) {
        const char *cr = memchr(s, '\r', len);
        const char *lf = memchr(s, '\n', len);
        if (cr) {
            if (lf && lf < cr)
                return lf;
            return cr;
        }
        if (lf)
            return lf;
        if (len <= CHUNK_SZ)
            break;
        len -= CHUNK_SZ;
        s += CHUNK_SZ;
    }
    return NULL;
}

int main(void) {
    char *buf = malloc(256);
    if (!buf) return 1;
    memset(buf, 'a', 255);
    buf[130] = '\n';
    const char *hit = find_eol_char(buf, 256);
    int ok = hit == buf + 130;
    free(buf);
    return ok ? 0 : 2;
}

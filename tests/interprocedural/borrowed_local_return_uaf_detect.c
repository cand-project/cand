#include <stdlib.h>
#include <string.h>

/*
 * Milestone #73 (ADR-0031), Area R adversarial: the returned borrow
 * keeps its caller-side lifetime link.
 *
 * Same helper shape as borrowed_local_return_safe.c, but the caller
 * frees the buffer and then dereferences the returned borrow. After
 * the Area R repair the return resolves to BorrowFromArg(0) and the
 * use-after-free must be detected as CAND-B002 (not masked by B003).
 *
 * Required verdicts:
 *   TODAY (pre-fix): FAIL (recorded findings in the milestone
 *                    verification artifacts; B003 and/or B002).
 *   AFTER  (post-fix): FAIL with the use-after-free detection
 *                    (CAND-B002), not B003.
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
    free(buf);
    return hit ? *hit : 0; /* use-after-free of the returned borrow */
}

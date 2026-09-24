#include <stdlib.h>

/*
 * Milestone #73 (ADR-0031), Area C: integer-delta cursor advance.
 *
 * A malloc'd buffer walked by a cursor that advances via `p += 16`
 * (stride form) and `p++` (bump form) with reads through the cursor
 * while the buffer is alive and a single free of the base. Advancing a
 * cursor by a pure integer delta cannot rebind it to another object,
 * so the walk must be decidable.
 *
 * Required verdicts:
 *   TODAY (pre-fix): INCOMPLETE via pointer-arithmetic-reassignment.
 *   AFTER  (post-fix): PASS.
 */
static int sum_stride16(const char *base, size_t n) {
    const char *p = base;
    int total = 0;
    for (size_t i = 0; i + 16 <= n; i += 16) {
        total += (unsigned char)*p;
        p += 16;
    }
    return total;
}

static int sum_bump(const char *base) {
    const char *p = base;
    int total = (unsigned char)*p;
    p++;
    total += (unsigned char)*p;
    return total;
}

int main(void) {
    char *buf = malloc(32);
    if (!buf) return 1;
    for (int i = 0; i < 32; i++) buf[i] = (char)i;
    int r = sum_stride16(buf, 32) + sum_bump(buf);
    free(buf);
    return r ? 0 : 2;
}

#include <stdlib.h>

/*
 * Milestone #73 (ADR-0031), Area C: advance round-trip.
 *
 * `p += 1; p -= 1;` returns the cursor to the base address, but the
 * Area C relation lattice has no path Interior -> Base (joinBinding
 * stays fail-closed), so the relation stays Interior and the free is
 * still flagged by the exact-base-required destruction predicate. This
 * is a DOCUMENTED spurious INCOMPLETE pinned so any future change to
 * the relation lattice is deliberate (plan risk list).
 *
 * Required verdicts:
 *   TODAY (pre-fix): INCOMPLETE.
 *   AFTER  (post-fix): INCOMPLETE (documented spurious verdict).
 */
static void roundtrip_free(char *base) {
    char *p = base;
    p += 1;
    p -= 1;
    free(p);
}

int main(void) {
    char *buf = malloc(16);
    if (!buf) return 1;
    roundtrip_free(buf);
    return 0;
}

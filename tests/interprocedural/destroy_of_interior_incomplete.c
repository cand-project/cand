#include <stdlib.h>

/*
 * Milestone #73 (ADR-0031), Area C: destroy of an interior cursor.
 *
 * No reviewed bundle in contracts/bundles/ carries a destroys-effect
 * symbol callable from plain C (contracts/libc.yaml's free is handled
 * by the dedicated handleFree path), so the destroy effect is
 * exercised through a same-TU verified summary: destroy_value's body
 * `free(p)` gives it a Destroy param-0 summary (the
 * destructor_wrapper_uaf.c mechanism).
 *
 * The cursor advances by a pure integer delta (relation Interior after
 * the Area C repair) and is then passed to the destroying callee; the
 * exact-base-required destruction predicate in destroyBinding must
 * flag the destroy-of-interior.
 *
 * Required verdicts:
 *   TODAY (pre-fix): INCOMPLETE (poisoned cursor; exact rows recorded
 *                    in the milestone verification artifacts).
 *   AFTER  (post-fix): INCOMPLETE (destroy-of-interior obligation;
 *                      never a PASS).
 */
static void destroy_value(int *p) { free(p); }

static void destroy_advanced(int *base) {
    int *p = base;
    p += 1;
    destroy_value(p);
}

int main(void) {
    int *buf = malloc(4 * sizeof(int));
    if (!buf) return 1;
    destroy_advanced(buf);
    return 0;
}

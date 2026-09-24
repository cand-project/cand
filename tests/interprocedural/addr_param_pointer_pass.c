#include <stdlib.h>
#include <string.h>

/*
 * Milestone #73 (ADR-0031), Area A pin-pass twin: address-of-POINTER-
 * parameter at a Borrow-effect call argument is ALREADY silent today
 * (findTrackedBinding resolves the seeded pointer-param object), and
 * must stay silent after the Area A repair (the fix is scoped to the
 * binding == nullptr path only).
 *
 * memcmp(&p1, &p2, sizeof(void *)) with two pointer parameters holding
 * heap pointers (the address-of-local twin is pinned by
 * tests/contracts/fixtures/socket_borrow_safe.c).
 *
 * Required verdicts:
 *   TODAY (pre-fix): PASS with the merged reviewed bundles.
 *   AFTER  (post-fix): PASS (must not regress).
 */
static int same_target(void *p1, void *p2) {
    return memcmp(&p1, &p2, sizeof(void *)) == 0;
}

int main(void) {
    void *a = malloc(8);
    void *b = malloc(8);
    if (!a || !b) { free(a); free(b); return 1; }
    int r = same_target(a, b);
    free(a);
    free(b);
    return r ? 0 : 2;
}

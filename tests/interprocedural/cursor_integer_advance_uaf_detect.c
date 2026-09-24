#include <stdlib.h>

/*
 * Milestone #73 (ADR-0031), Area C adversarial: integer-delta cursor
 * advance must PRESERVE the lifetime link to the parent object.
 *
 * The cursor advances by a pure integer delta, the base is freed, and
 * the advanced cursor is then dereferenced: a use-after-free. The Area
 * C repair (relation Interior keeps the parent object id) must keep
 * detecting this; only the verdict word changes from the pre-fix
 * obligations to the B002 use-after-free finding.
 *
 * Required verdicts:
 *   TODAY (pre-fix): INCOMPLETE (the poisoned cursor masks the UAF as
 *                    obligations: pointer-arithmetic-reassignment and
 *                    ambiguous-alias-target, no findings).
 *   AFTER  (post-fix): FAIL (CAND-B002 use-after-free class).
 *
 * NOTE: the caller stores the call result before testing it; a
 * destroying call placed directly in a ternary condition triggers an
 * unrelated pre-existing double-application quirk on main (spurious
 * CAND-T003), which this fixture deliberately avoids.
 */
static int walk_then_use(char *base) {
    char *p = base;
    p += 16;
    free(base);
    return (unsigned char)*p; /* use-after-free through the advanced cursor */
}

int main(void) {
    char *buf = malloc(32);
    if (!buf) return 1;
    int r = walk_then_use(buf);
    return r ? 0 : 2;
}

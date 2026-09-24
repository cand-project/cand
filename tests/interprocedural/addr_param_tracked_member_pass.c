#include <stdlib.h>
#include <string.h>

/*
 * Milestone #73 (ADR-0031), Area A pin-pass twin: address of a tracked
 * struct member reached through pointer parameters at a Borrow-effect
 * call argument is ALREADY silent today (findTrackedBinding resolves
 * the seeded pointer-param object and its member), and must stay silent
 * after the Area A repair.
 *
 * memcmp(&s->f, &t->f, 4) with s/t pointing into tracked heap objects.
 *
 * Required verdicts:
 *   TODAY (pre-fix): PASS with the merged reviewed bundles.
 *   AFTER  (post-fix): PASS (must not regress).
 */
struct box {
    int f;
};

static int same_field(const struct box *s, const struct box *t) {
    return memcmp(&s->f, &t->f, sizeof(int)) == 0;
}

int main(void) {
    struct box *s = malloc(sizeof *s);
    struct box *t = malloc(sizeof *t);
    if (!s || !t) { free(s); free(t); return 1; }
    s->f = 1;
    t->f = 1;
    int r = same_field(s, t);
    free(s);
    free(t);
    return r ? 0 : 2;
}

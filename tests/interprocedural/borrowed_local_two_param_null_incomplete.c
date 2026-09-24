#include <stdlib.h>

/*
 * Milestone #73 (ADR-0031), Area R: conditional with a NULL arm over
 * two parameters.
 *
 * `return c ? p : (c2 ? NULL : q);` joins {0, NULL, 1}; NULL alone
 * must never make a singleton ({j, NULL} resolves only when j is the
 * single non-NULL member). This guards the NULL-absorption rule.
 *
 * Required verdicts:
 *   TODAY (pre-fix): INCOMPLETE.
 *   AFTER  (post-fix): INCOMPLETE.
 */
static int *pick(int *p, int *q, int c, int c2) {
    return c ? p : (c2 ? NULL : q);
}

int main(void) {
    int *a = malloc(sizeof(int));
    int *b = malloc(sizeof(int));
    if (!a || !b) { free(a); free(b); return 1; }
    int *r = pick(a, b, 0, 0);
    *r = 1;
    free(a);
    free(b);
    return 0;
}

/*
 * Milestone #61 / ADR-0027 control: conditional destroy still fails closed
 * to Unknown (H2); the caller stays blocked. Guards against the join rule
 * leaking into consume/destroy effects.
 */
#include <stdlib.h>
/* H2 control: conditional destroy must stay Unknown (fail-closed) */
static void f(int *p, int c) { if (c) free(p); }
int main(void) {
    int *q = malloc(sizeof(int));
    if (!q) return 1;
    f(q, 1);
    *q = 5;
    free(q);
    return 0;
}

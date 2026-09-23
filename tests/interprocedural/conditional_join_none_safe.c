/*
 * Milestone #61 / ADR-0027 regression: a parameter passed to a
 * no-effect callee only under a condition keeps param effect none; the
 * legal caller passes.
 */
#include <stdlib.h>
static void nop(int *p) { (void)p; }   /* param effect: no_ownership_effect */
/* conditional no-effect call: pre-C1 p=Unknown; post-C1 p=none (decided) */
static void f(int *p, int c) { if (c) nop(p); }
int main(void) {
    int *q = malloc(sizeof(int));
    if (!q) return 1;
    f(q, 1);
    free(q);
    return 0;
}

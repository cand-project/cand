/*
 * Milestone #61 / ADR-0027 regression: a parameter passed to a
 * borrow-effect callee only under a condition is at most borrowed overall,
 * so the summary stays decided (param effect borrow) and the legal caller
 * passes. Pre-rule this was collapsed to Unknown and blocked.
 */
#include <stdlib.h>
static void borrow_use(int *p) { int x = *p; (void)x; }
/* conditional borrow: pre-C1 p=Unknown (caller blocked); post-C1 p=borrow */
static void f(int *p) { if (p) borrow_use(p); }
int main(void) {
    int *q = malloc(sizeof(int));
    if (!q) return 1;
    f(q);
    free(q);
    return 0;
}

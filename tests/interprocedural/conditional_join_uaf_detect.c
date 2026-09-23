/*
 * Milestone #61 / ADR-0027 control (false-PASS probe): destroying a
 * pointer and then passing it to a conditionally-borrowing callee must be
 * detected (or at minimum blocked); with the callee decided the call
 * becomes a borrow use of a destroyed pointer and is a FAIL detection.
 */
#include <stdlib.h>
static void borrow_use(int *p) { int x = *p; (void)x; }
static void f(int *p) { if (p) borrow_use(p); }
int main(void) {
    int *q = malloc(sizeof(int));
    if (!q) return 1;
    free(q);
    f(q);      /* destroy-then-call: must NOT become PASS post-C1 */
    return 0;
}

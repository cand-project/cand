/*
 * Milestone #61 / ADR-0027 control: a parameter borrowed on one branch and
 * destroyed on the other must stay Unknown (conflict join); the caller
 * cannot see the destroy and must stay blocked.
 */
#include <stdlib.h>
static void borrow_use(int *p) { int x = *p; (void)x; }
/* branch-divided: borrow on one path, destroy on the other -> must stay Unknown */
static void f(int *p, int c) { if (c) borrow_use(p); else free(p); }
int main(void) {
    int *q = malloc(sizeof(int));
    if (!q) return 1;
    f(q, 0);      /* c==0: q freed inside f */
    *q = 1;       /* potential UAF the caller cannot see; f must stay blocked */
    return 0;
}

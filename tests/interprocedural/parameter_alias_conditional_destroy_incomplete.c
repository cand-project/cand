/*
 * Milestone #54 / ADR-0028 + ADR-0027 (H2 control, alias form):
 * conditional destruction through an alias joins to Unknown
 * (fail-closed); the caller stays blocked, exactly like the direct
 * conditional-destroy control (conditional_join_destroy_incomplete.c).
 */
#include <stdlib.h>
static void f(int *p, int c) { if (c) { int *q = p; free(q); } }
int main(void) {
    int *x = malloc(sizeof *x);
    if (!x) return 1;
    f(x, 1);
    *x = 5;
    free(x);
    return 0;
}

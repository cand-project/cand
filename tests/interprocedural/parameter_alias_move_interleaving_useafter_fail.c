/*
 * Milestone #54 / ADR-0028 soundness pin (companion to
 * parameter_alias_move_interleaving_safe.c): the interleaved move
 * must not lose the destruction bookkeeping. Using the argument after
 * the call is a use-after-destroy FAIL.
 */
#include <stdlib.h>
#define CAND_A(value) __attribute__((annotate(value)))
#define CAND_OWN CAND_A("cand:own")
#define CAND_MOVE(x) (x)
static void run(int *p) { int *q = p; int *m CAND_OWN = CAND_MOVE(p); free(q); (void)m; }
int main(void) {
    int *x = malloc(sizeof *x);
    if (!x) return 1;
    run(x);
    return *x;
}

/*
 * Milestone #54 / ADR-0028: an ownership move of the parameter
 * interleaved with the alias destruction. The destroy through the
 * alias is real: the object is dead after the call. This body has no
 * subsequent use (the moved owner m is only read as a pointer value,
 * which is outside the P0 temporal claim), so the program is legal
 * and must PASS -- the pre-repair CAND-O006 here was a false FAIL.
 * The companion parameter_alias_move_interleaving_useafter_fail.c
 * pins the soundness side: any use after the call must FAIL.
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
    return 0;
}

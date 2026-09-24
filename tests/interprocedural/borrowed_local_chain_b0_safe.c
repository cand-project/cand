#include <stdlib.h>

/*
 * Milestone #73 (ADR-0031), Area R: same-TU local-copy chain without
 * contracts (the b0 delta documenter).
 *
 * `t = helper(p); ...; return t;` where helper's verified summary
 * returns BorrowFromArg(0). The Area R dataflow's call transfer
 * (O_v |= O_{arg_k} on a BorrowFromArg callee) plus the local-copy
 * chain newly resolve outer's return; call-shaped returns stay blocked
 * at handleReturn. This fixture documents the b0 boundary: whether the
 * chain already resolves today is recorded in the milestone
 * verification artifacts.
 *
 * Required verdicts:
 *   TODAY (pre-fix): FAIL (CAND-B003: the local-copy chain does not
 *                    resolve the return today; the plan's pass-or-
 *                    incomplete prediction was wrong — verified on
 *                    current main, see the S3 verification artifacts).
 *   AFTER  (post-fix): PASS.
 */
static int *helper(int *p) {
    return p;
}

static int *outer(int *p) {
    int *t = helper(p);
    return t;
}

int main(void) {
    int *buf = malloc(sizeof(int));
    if (!buf) return 1;
    int *r = outer(buf);
    *r = 1;
    free(buf);
    return 0;
}

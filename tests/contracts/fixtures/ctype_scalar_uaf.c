#include <stdlib.h>
#include <ctype.h>

/*
 * ADVERSARIAL fixture for `no_ownership_effect` on by-value scalar
 * parameters (tolower, contracts/bundles/libc-ctype.yaml).
 *
 * The trusted claim is only that the CALLEE receives a scalar copy and can
 * retain nothing. The argument expression `*p` is evaluated on the CALLER
 * side; this read must still be validated by the ordinary expression walk.
 * Reading through a freed pointer inside the argument must therefore be a
 * FAIL (use-after-free), never a PASS, with the contracts active.
 *
 * Expected: fail (CAND-B003 use-after-free), with or without contracts.
 */
int run(char *p) {
    free(p);
    return tolower((unsigned char)*p);
}

int main(void) { return 0; }

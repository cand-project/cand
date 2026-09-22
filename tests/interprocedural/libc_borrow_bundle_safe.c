#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * Guards the reviewed libc borrow bundles (contracts/bundles/, merged by
 * scripts/contracts/merge_contracts.py). With the bundles active,
 * memcpy/memmove interoperating with tracked
 * heap allocations is treated as a borrow (a clean, decidable read/write of the
 * pointee bytes within the call) rather than an opaque
 * unknown-call-with-tracked-pointer obligation.
 *
 * This fixture must PASS with the merged reviewed bundles. Without
 * the bundles it is INCOMPLETE; the contract test pins the net effect without
 * weakening any soundness obligation (each allocation here is freed exactly
 * once and never retained by the borrow).
 */
int run(void) {
    char *a = malloc(16);
    if (!a) return 1;
    char *b = malloc(16);
    if (!b) { free(a); return 1; }
    memset(a, 0, 16);
    memcpy(b, a, 16);
    strncpy(b, a, 15); /* returns b (borrow-from-arg 0); guards the returns declaration */
    /* Untracked variadic argument only: a tracked pointer at a variadic
     * position is an unmodelled escape (see variadic_argument_escape.c). */
    snprintf(b, 16, "%d", 42);
    int ok = memcmp(b, "42", 3) == 0;
    free(a);
    free(b);
    return ok ? 0 : 2;
}

int main(void) { return run(); }

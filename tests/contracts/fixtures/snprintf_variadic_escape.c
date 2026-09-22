#include <stdlib.h>
#include <stdio.h>

/*
 * ADVERSARIAL fixture for the reviewed libc-stdio bundle
 * (contracts/bundles/libc-stdio.yaml).
 *
 * The contract covers only the three DECLARED FIXED parameters of snprintf
 * (buffer borrow, size scalar, format borrow). A tracked pointer passed at
 * a VARIADIC position is an unmodelled escape: the verifier caps contract
 * parameter lists at the declared fixed parameters (the incident-#53
 * companion repair), so this must stay INCOMPLETE, never PASS.
 */
int run(void) {
    char *buf = malloc(16);
    char *tracked = malloc(4);
    if (!buf || !tracked) { free(buf); free(tracked); return 1; }
    snprintf(buf, 16, "%s", tracked);
    free(tracked);
    free(buf);
    return 0;
}

int main(void) { return run(); }

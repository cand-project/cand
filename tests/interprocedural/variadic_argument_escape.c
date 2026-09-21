#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * Variadic-argument escape matrix (direct-call summary path).
 *
 * Arguments at positions beyond the callee's modelled parameter list
 * (variadic slots) carry no contract or body-summary param effect, yet the
 * callee may read or retain tracked storage passed there. Every tracked
 * pointer passed at such a position must produce an escape obligation, so
 * no case here may PASS.
 *
 *   ESCAPE:   live tracked pointer as a variadic argument to a local
 *             transparent variadic function -> INCOMPLETE.
 *   DEAD:     freed tracked pointer as a variadic argument -> INCOMPLETE.
 *             Regression guard for the variadic false-PASS incident: the
 *             pre-fix summary path skipped these positions entirely, so
 *             this case received PASS with zero obligations on v0.2.0.
 *   SNPRINTF: freed tracked pointer as snprintf's %s argument with the
 *             reviewed libc borrow bundle active -> INCOMPLETE. This is
 *             the exact vector that exposed the incident (snprintf reads
 *             the freed pointee; ASan confirms heap-use-after-free).
 */

static void sink(const char *fmt, ...) { (void)fmt; }

#ifdef CASE_VA_SNPRINTF
int run(void) {
    char *p = malloc(16);
    char *b = malloc(16);
    if (!p || !b) return 1;
    memset(p, 'x', 15);
    p[15] = 0;
    free(p);
    snprintf(b, 16, "%s", p); /* reads freed p at runtime */
    return b[0] ? 0 : 2;
}
#else
int run(void) {
    char *p = malloc(16);
    if (!p) return 1;
#ifdef CASE_VA_DEAD
    free(p);
#endif
    sink("%s", p); /* tracked pointer at an unmodelled variadic position */
    return 0;
}
#endif

int main(void) { return run(); }

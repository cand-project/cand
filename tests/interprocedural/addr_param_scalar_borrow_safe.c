#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

/*
 * Milestone #73 (ADR-0031), Area A: address-of-scalar-parameter at a
 * Borrow-effect call argument.
 *
 * setsockopt(&on) with a by-value int parameter (the libevent evutil.c:3211
 * shape) and memcmp(&r1, &r2) with by-value double parameters (the
 * sqlite3.c:85990 sqlite3RealSameAsInt shape). With the reviewed bundles
 * active the pointer arguments are borrow claims on pointer-free stack
 * slots; reading or writing a scalar slot is ownership-neutral.
 *
 * Required verdicts:
 *   TODAY (pre-fix): INCOMPLETE via unmodelled-pointer-parameter (with
 *                    the merged reviewed bundles; without contracts no
 *                    borrow claim is made and the file passes).
 *   AFTER  (post-fix): PASS with the merged reviewed bundles.
 */
static int set_reuseaddr(int fd, int on) {
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, (socklen_t)sizeof on);
}

static int real_same_as_int(double r1, double r2) {
    return memcmp(&r1, &r2, sizeof r1) == 0;
}

int main(void) {
    int fd = 0; /* static analysis only: the descriptor value is irrelevant */
    int ok = set_reuseaddr(fd, 1);
    ok |= real_same_as_int(1.0, (double)1);
    return ok ? 0 : 2;
}

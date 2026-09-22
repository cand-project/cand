#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

/*
 * Positive conformance fixture for the reviewed POSIX socket bundle
 * (contracts/bundles/posix-socket.yaml).
 *
 * connect/setsockopt/getsockopt/send/recv with tracked buffers are borrows
 * within the call; the socklen_t out-param of getsockopt has a scalar
 * pointee, so no pointer-output is granted. Each allocation is freed once;
 * the file must PASS with the merged reviewed contracts and be INCOMPLETE
 * without them.
 */
int run(void) {
    char *opt = malloc(8);
    char *msg = malloc(8);
    if (!opt || !msg) { free(opt); free(msg); return 1; }
    memset(msg, 'm', 8);

    struct sockaddr_in addr;
    socklen_t len = 8;

    int rc = 0;
    rc |= setsockopt(0, 0, 0, opt, 8);
    rc |= getsockopt(0, 0, 0, opt, &len);
    rc |= send(0, msg, 8, 0);
    rc |= recv(0, msg, 8, 0);
    rc |= connect(0, (const struct sockaddr *)&addr, (socklen_t)sizeof addr);

    free(opt);
    free(msg);
    return rc ? 0 : 2; /* result value is irrelevant: static analysis only */
}

int main(void) { return run(); }

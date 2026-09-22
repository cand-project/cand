#include <stdlib.h>
#include <unistd.h>

/*
 * ADVERSARIAL fixture for `no_ownership_effect` on by-value scalar
 * parameters (close, contracts/bundles/posix-io.yaml).
 *
 * `close(c->fd)` evaluates `c->fd` on the CALLER side; after `c` is freed
 * that member read is a use-after-free and must FAIL with the contracts
 * active. The trusted contract covers only what the kernel does with the
 * integer descriptor value, never the caller-side argument evaluation.
 */
struct conn { int fd; char buf[16]; };

int run(struct conn *c) {
    int out = 0;
    free(c);
    if (close(c->fd) != 0) out = 1;
    return out;
}

int main(void) { return 0; }

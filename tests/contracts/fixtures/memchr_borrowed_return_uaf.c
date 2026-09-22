#include <stdlib.h>
#include <string.h>

/*
 * ADVERSARIAL fixture for borrowed-return-from-parameter
 * (memchr, contracts/bundles/libc-memory.yaml).
 *
 * The trusted claim is that the return value aliases parameter 0 (an
 * interior pointer into the caller's buffer) and introduces no new
 * ownership. Using the result after the buffer is freed is therefore a
 * use-after-free and must FAIL with the contracts active.
 */
int run(char *buf) {
    char *hit = memchr(buf, 'x', 16);
    free(buf);
    return hit ? *hit : 0;
}

int main(void) { return 0; }

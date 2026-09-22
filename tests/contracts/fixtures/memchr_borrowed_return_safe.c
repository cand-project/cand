#include <stdlib.h>
#include <string.h>

/*
 * Positive fixture for borrowed-return-from-parameter
 * (memchr/strchr, contracts/bundles/libc-memory.yaml and libc-string.yaml).
 *
 * The returned interior pointer borrows the caller's buffer; using it while
 * the buffer is alive and freeing the buffer exactly once must PASS with
 * the contracts active.
 */
int run(void) {
    char *buf = malloc(16);
    if (!buf) return 1;
    memset(buf, 'a', 15);
    buf[15] = '\0';
    char *hit = memchr(buf, 'a', 15);
    char *tail = strchr(buf, '\0');
    int ok = hit == buf && tail == buf + 15;
    free(buf);
    return ok ? 0 : 2;
}

int main(void) { return run(); }

#include <stdlib.h>
#include <string.h>

/*
 * Positive fixture for the string-search borrowed-return class
 * (strrchr/strstr/strpbrk in contracts/bundles/libc-string.yaml and
 * memrchr in contracts/bundles/libc-memory.yaml) plus the bounded-write
 * strlcpy (libc-string.yaml, POSIX.1-2024).
 *
 * The returned interior pointers borrow the caller's buffer; using them
 * while the buffer is alive and freeing the buffer exactly once must
 * PASS with the contracts active. strlcpy writes at most size-1 bytes
 * into the destination within the call and returns a scalar.
 *
 * Compiled with -std=gnu11: memrchr is a GNU extension and strlcpy is
 * POSIX.1-2024; neither is visible under strict -std=c11 on glibc.
 */
int run(void) {
    char *buf = malloc(16);
    if (!buf) return 1;
    memcpy(buf, "hash/key:value", 14);
    buf[14] = '\0';

    char *colon = strrchr(buf, ':');    /* last occurrence, interior */
    char *key = strstr(buf, "key");     /* interior into param 0 */
    char *sep = strpbrk(buf, "/:");     /* first of the set, interior */
    char *last = memrchr(buf, 'a', 14); /* backward scan, interior */
    size_t lc = strlcpy(buf, "done", 8); /* bounded write, scalar return */

    int ok = colon == buf + 8 && key == buf + 5 && sep == buf + 4
             && last == buf + 10 && lc == 4 && buf[4] == '\0';
    free(buf);
    return ok ? 0 : 2;
}

int main(void) { return run(); }

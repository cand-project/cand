#include <stdlib.h>
#include <string.h>

/*
 * ADVERSARIAL fixture for the string-search borrowed-return class
 * (strrchr/strstr/strpbrk, contracts/bundles/libc-string.yaml; memrchr,
 * contracts/bundles/libc-memory.yaml).
 *
 * The trusted claim is that each return value aliases parameter 0 (an
 * interior pointer into the caller's buffer) and introduces no new
 * ownership. Using any of the results after the buffer is freed is
 * therefore a use-after-free and must FAIL with the contracts active.
 *
 * Compiled with -std=gnu11 (memrchr is a GNU extension).
 */
int run(char *buf) {
    char *p = strrchr(buf, ':');
    char *q = strstr(buf, "key");
    char *r = strpbrk(buf, "/:");
    char *s = memrchr(buf, 'a', 16);
    free(buf);
    return (p ? *p : 0) + (q ? *q : 0) + (r ? *r : 0) + (s ? *s : 0);
}

int main(void) { return 0; }

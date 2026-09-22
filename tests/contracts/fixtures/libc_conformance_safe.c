#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdio.h>

/*
 * Positive conformance fixture for the reviewed external-API boundary
 * bundles (contracts/libc.yaml plus contracts/bundles, merged by
 * scripts/contracts/merge_contracts.py).
 *
 * Every external call here is one the reviewed bundles classify:
 *   memcpy/memmove/memset/memcmp/memchr  borrow + borrowed-return-from-param
 *   strlen/strnlen/strchr/strncmp/strncpy/strcasecmp/strncasecmp
 *   tolower/toupper                       no_ownership_effect (scalar)
 *   snprintf                              borrow + scalar size
 *
 * With the merged reviewed contracts this file must PASS: the borrows are
 * decidable, each allocation is freed exactly once, and no retention or
 * ownership transfer crosses any boundary. Without contracts the same file
 * is INCOMPLETE (fail-closed unknown-call obligations).
 */
int run(void) {
    char *a = malloc(32);
    if (!a) return 1;
    char *b = malloc(32);
    if (!b) { free(a); return 1; }

    memset(a, 'x', 16);
    memcpy(b, a, 16);
    memmove(b + 4, b, 8);

    size_t n = strnlen(b, 16);
    char *hit = memchr(b, 'x', n);
    char *slash = strchr(a, 'x'); /* borrowed return from a TRACKED buffer;
                                   * an untracked literal source stays an
                                   * unknown-pointer-return-ownership
                                   * obligation by design (fail-closed) */
    int cmp = strncmp(b, a, 4);
    int ci = strcasecmp("AbC", "abc");
    int ci2 = strncasecmp("AbC", "abd", 2);
    (void)ci2;

    strncpy(b, "hi", 2); /* borrowed return from param 0 */
    int lower = tolower('A');
    int upper = toupper('a');
    snprintf(b, 32, "%d", 42);

    int ok = (hit != NULL) && (slash != NULL) && cmp != 0 && ci == 0
             && lower == 'a' && upper == 'A';
    free(a);
    free(b);
    return ok ? 0 : 2;
}

int main(void) { return run(); }

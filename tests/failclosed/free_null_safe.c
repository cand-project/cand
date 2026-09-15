#include <stdlib.h>

/*
 * `free(NULL)` is defined as a no-op by ISO C: KNOWN SAFE, allowed.
 * Expected P0.1 result: PASS.
 */
int main(void)
{
    free(NULL);
    free(0);

    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }
    free(p);

    /* Note: free(q) where q is a *variable* holding NULL is not covered:
     * P0 has no dataflow, so the nullness of a variable is unknown and the
     * free is INCOMPLETE (free-untracked-pointer). That is acceptable
     * fail-closed behavior for P0.1. */

    return 0;
}

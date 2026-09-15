#include <stdlib.h>

/*
 * P0.2: `p = NULL` is modeled as a known-safe release, so the following
 * free(NULL) is a defined no-op rather than an unresolved obligation.
 *
 * Expected: PASS (ASan clean).
 */
int main(void)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    *p = 1;
    free(p);
    p = 0;
    free(p);
    return 0;
}

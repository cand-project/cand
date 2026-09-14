#include <stdlib.h>

/*
 * A pointer parameter has no ownership context in P0: the callee cannot be
 * proven to own (or not own) the object. The free must not be silently
 * accepted.
 *
 * Required P0.1 result: INCOMPLETE (free-untracked-pointer).
 */
void destroy(int *p)
{
    free(p);
}

int main(void)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }
    destroy(p);
    return 0;
}

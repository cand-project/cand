#include <stdlib.h>
#include <cand/cand.h>

int conditional_stale_alias(unsigned count, int enabled)
{
    int *stale = NULL;
    for (unsigned i = 0; i < count; ++i) {
        if (!enabled) continue;
        int *p CAND_OWN = malloc(sizeof *p);
        if (p == NULL) continue;
        if (stale != NULL) {
            int result = *stale;
            free(p);
            return result;
        }
        stale = p;
        free(p);
    }
    return 0;
}

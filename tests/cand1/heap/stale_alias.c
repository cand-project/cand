#include <stdlib.h>
#include <cand/cand.h>

int stale_alias(unsigned count)
{
    int *stale = NULL;
    for (unsigned i = 0; i < count; ++i) {
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

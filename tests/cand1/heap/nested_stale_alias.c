#include <stdlib.h>
#include <cand/cand.h>

int nested_stale_alias(unsigned outer, unsigned inner)
{
    int *stale = NULL;
    for (unsigned i = 0; i < outer; ++i) {
        for (unsigned j = 0; j < inner; ++j) {
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
    }
    return 0;
}

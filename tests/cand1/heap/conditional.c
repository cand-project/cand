#include <stdlib.h>
#include <cand/cand.h>

void conditional_allocation(unsigned count, int enabled)
{
    volatile int sink = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (enabled) {
            int *p CAND_OWN = malloc(sizeof *p);
            if (p == NULL) continue;
            sink ^= *p;
            free(p);
        }
    }
    (void)sink;
}

#include <stdlib.h>
#include <cand/cand.h>

void safe_per_iteration(unsigned count)
{
    volatile int sink = 0;
    for (unsigned i = 0; i < count; ++i) {
        int *p CAND_OWN = malloc(sizeof *p);
        if (p == NULL) continue;
        *p = (int)i;
        sink ^= *p;
        free(p);
    }
    (void)sink;
}

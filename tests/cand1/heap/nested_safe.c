#include <stdlib.h>
#include <cand/cand.h>

void nested_safe(unsigned outer, unsigned inner)
{
    volatile int sink = 0;
    for (unsigned i = 0; i < outer; ++i) {
        for (unsigned j = 0; j < inner; ++j) {
            int *p CAND_OWN = malloc(sizeof *p);
            if (p == NULL) continue;
            *p = (int)(i + j);
            sink ^= *p;
            free(p);
        }
    }
    (void)sink;
}

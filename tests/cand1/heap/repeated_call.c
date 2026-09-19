#include <stdlib.h>
#include <cand/cand.h>

static void one_call(void)
{
    int *p CAND_OWN = malloc(sizeof *p);
    if (p != NULL) free(p);
}

void repeated_calls(unsigned count)
{
    for (unsigned i = 0; i < count; ++i) one_call();
}

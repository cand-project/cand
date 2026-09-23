#include <stdlib.h>

#define CAND_TAKES __attribute__((annotate("cand:takes")))
#define CAND_DESTROYS __attribute__((annotate("cand:destroys")))

/* Conflicting per-parameter annotations across redeclarations: one
 * declaration claims the parameter is consumed, the other that it is
 * destroyed. Fail closed. */
extern void reviewed_conflicting_param(int *value CAND_TAKES);
extern void reviewed_conflicting_param(int *value CAND_DESTROYS);

int main(void)
{
    int *value = malloc(sizeof *value);
    if (value == NULL) return 0;
    reviewed_conflicting_param(value);
    return 0;
}

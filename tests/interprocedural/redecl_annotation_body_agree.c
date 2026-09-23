#include <stdlib.h>

#define CAND_RETURNS_OWN __attribute__((annotate("cand:returns_own")))

/* Same-TU prototype annotation with an agreeing body: the inherited
 * annotation and the body scan agree; the boundary resolves through the
 * ordinary same-TU summary path (no review manifest needed). Unchanged
 * behavior, pinned. */
int *redecl_annotated_body_agree(void) CAND_RETURNS_OWN;
int *redecl_annotated_body_agree(void)
{
    return malloc(sizeof(int));
}

int main(void)
{
    int *p = redecl_annotated_body_agree();
    if (p == NULL) return 0;
    free(p);
    return 0;
}

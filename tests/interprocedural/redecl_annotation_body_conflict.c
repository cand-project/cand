#include <stdlib.h>

#define CAND_RETURNS_OWN __attribute__((annotate("cand:returns_own")))

/* Same-TU prototype annotation with a disagreeing body: the body scan
 * and the (inherited) annotation contradict each other; the existing
 * combineReturn conflict path fails closed. Unchanged behavior, pinned. */
static int storage;

int *redecl_annotated_body_conflict(void) CAND_RETURNS_OWN;
int *redecl_annotated_body_conflict(void)
{
    return &storage;
}

int main(void)
{
    return redecl_annotated_body_conflict() != NULL;
}

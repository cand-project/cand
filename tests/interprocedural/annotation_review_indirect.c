#include <stdlib.h>

#define CAND_RETURNS_OWN __attribute__((annotate("cand:returns_own")))

/* Indirect calls do not resolve to declaration summaries; an annotated
 * declaration of the pointed-to function must not change the indirect
 * call's classification. */
extern CAND_RETURNS_OWN int *reviewed_indirect(void);

int main(void)
{
    int *(*fn)(void) = reviewed_indirect;
    int *p = fn();
    return p != NULL;
}

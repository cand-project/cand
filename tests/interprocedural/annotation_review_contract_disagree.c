#include <stdlib.h>

#define CAND_RETURNS_OWN __attribute__((annotate("cand:returns_own")))

/* Reviewed annotation and reviewed contract disagree: the contract says
 * the return is borrowed, the reviewed annotation says owned. Trusted
 * sources must not silently resolve the disagreement; fail closed. */
extern CAND_RETURNS_OWN int *reviewed_disagree(void);

int main(void)
{
    int *p = reviewed_disagree();
    return p != NULL;
}

#include <stdlib.h>

#define CAND_RETURNS_OWN __attribute__((annotate("cand:returns_own")))
#define CAND_DESTROYS __attribute__((annotate("cand:destroys")))

/* Reviewed annotation and reviewed contract agree on every fact: the
 * contract keeps provenance and the boundary resolves. */
extern CAND_RETURNS_OWN int *reviewed_agree(void);
extern void reviewed_destroy_agree(int *value CAND_DESTROYS);

int main(void)
{
    int *p = reviewed_agree();
    if (p == NULL) return 0;
    reviewed_destroy_agree(p);
    return 0;
}

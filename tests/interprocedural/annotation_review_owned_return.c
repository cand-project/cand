#include <stdlib.h>

#define CAND_RETURNS_OWN __attribute__((annotate("cand:returns_own")))
#define CAND_DESTROYS __attribute__((annotate("cand:destroys")))

/* External (no body in this TU) ownership boundary expressed with
 * reviewed declaration-site annotations. Without the annotation review
 * manifest this stays INCOMPLETE (candidate-only annotations never gain
 * PASS authority); with the matching manifest it resolves exactly like
 * the equivalent reviewed contract. */
extern CAND_RETURNS_OWN int *reviewed_owned(void);
extern void reviewed_destroy(int *value CAND_DESTROYS);

int main(void)
{
    int *p = reviewed_owned();
    if (p == NULL) return 0;
    reviewed_destroy(p);
    return 0;
}

#include <stdlib.h>

#define CAND_RETURNS_OWN __attribute__((annotate("cand:returns_own")))

/* A pointer-to-pointer return shape stays outside the reviewed
 * annotation vocabulary (#41 scope guard) even with a matching
 * manifest entry. */
extern CAND_RETURNS_OWN int **reviewed_slots(void);

int main(void)
{
    int **slots = reviewed_slots();
    return slots != NULL;
}

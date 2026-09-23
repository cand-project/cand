#include <stdlib.h>

#define CAND_RETURNS_OWN __attribute__((annotate("cand:returns_own")))

/* Unspecified-parameter (K&R) declarations carry no reviewable
 * parameter fact; seeding is refused and the boundary stays
 * INCOMPLETE. */
extern CAND_RETURNS_OWN int *reviewed_knr();

int main(void)
{
    int *p = reviewed_knr();
    return p != NULL;
}

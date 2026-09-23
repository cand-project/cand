#include <stdlib.h>

#define CAND_RETURNS_OWN __attribute__((annotate("cand:returns_own")))

/* Pointer-to-pointer output parameters stay outside the reviewed
 * annotation vocabulary (#41 scope guard): a reviewed manifest entry
 * must not make an out-parameter boundary pass. */
extern CAND_RETURNS_OWN int *reviewed_out(int **out);

int main(void)
{
    int *slot = NULL;
    int *result = reviewed_out(&slot);
    return (slot != NULL) + (result != NULL);
}

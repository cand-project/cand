#include <stdlib.h>

#define CAND_RETURNS_OWN __attribute__((annotate("cand:returns_own")))
#define CAND_DESTROYS __attribute__((annotate("cand:destroys")))

/* realloc's conditional reallocation semantics stay outside the
 * annotation vocabulary (P0.4); a reviewed annotation must not claim
 * an owned return for it. Mirrors the contract loader's realloc guard. */
extern CAND_RETURNS_OWN void *realloc(void *ptr, size_t size);

int main(void)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) return 0;
    int *q = realloc(p, 2 * sizeof *q);
    return q != NULL;
}

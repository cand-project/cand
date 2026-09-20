#include <stdlib.h>

#define CAND_RETURNS_OWN __attribute__((annotate("cand:returns_own")))
extern CAND_RETURNS_OWN int *external_annotated(void);

int main(void)
{
    int *p = external_annotated();
    free(p);
    return 0;
}

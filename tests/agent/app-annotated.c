#include <stdlib.h>

#define CAND_RETURNS_OWN __attribute__((annotate("cand:returns_own")))
#define CAND_DESTROYS __attribute__((annotate("cand:destroys")))

extern CAND_RETURNS_OWN int *reviewed_create(void);
extern void reviewed_destroy(int *item CAND_DESTROYS);

int main(void)
{
    int *item = reviewed_create();
    if (!item) return 0;
    *item = 23;
    reviewed_destroy(item);
    return 0;
}

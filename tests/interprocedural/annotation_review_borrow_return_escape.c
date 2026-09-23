#include <stdlib.h>

#define CAND_RETURNS_BORROW_FROM_0 __attribute__((annotate("cand:returns_borrow_from:0")))
#define CAND_BORROW __attribute__((annotate("cand:borrow")))

/* The reviewed borrowed return escapes the argument lifetime: the view
 * is dereferenced after the borrowed-from owner was destroyed. */
extern CAND_RETURNS_BORROW_FROM_0 int *reviewed_view(int *base CAND_BORROW);

int main(void)
{
    int *owner = malloc(sizeof *owner);
    if (owner == NULL) return 0;
    int *view = reviewed_view(owner);
    free(owner);
    return *view;
}

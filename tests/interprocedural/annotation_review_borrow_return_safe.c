#include <stdlib.h>

#define CAND_RETURNS_BORROW_FROM_0 __attribute__((annotate("cand:returns_borrow_from:0")))
#define CAND_BORROW __attribute__((annotate("cand:borrow")))

/* Reviewed borrowed-return boundary: the returned pointer borrows from
 * parameter 0, and the callee only reads the argument. The caller keeps
 * the argument alive across the view. */
extern CAND_RETURNS_BORROW_FROM_0 int *reviewed_view(int *base CAND_BORROW);

int main(void)
{
    int *owner = malloc(sizeof *owner);
    if (owner == NULL) return 0;
    int *view = reviewed_view(owner);
    int value = *view;
    free(owner);
    return value;
}

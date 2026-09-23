#include <stdlib.h>

#define CAND_RETURNS_BORROW_FROM_0 __attribute__((annotate("cand:returns_borrow_from:0")))

/* Reviewed borrowed-return boundary: the returned pointer borrows from
 * parameter 0. The caller keeps the argument alive across the view. */
extern CAND_RETURNS_BORROW_FROM_0 int *reviewed_view(int *base);

int main(void)
{
    int storage = 1;
    int *view = reviewed_view(&storage);
    return *view;
}

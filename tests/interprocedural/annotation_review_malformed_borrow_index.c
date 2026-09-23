#include <stdlib.h>

#define CAND_RETURNS_BORROW_FROM_99 __attribute__((annotate("cand:returns_borrow_from:99")))

/* Malformed annotation: the borrow-from index is out of range for the
 * declaration's parameter count. Fail closed. */
extern CAND_RETURNS_BORROW_FROM_99 int *reviewed_bad_index(void);

int main(void)
{
    int *p = reviewed_bad_index();
    return p != NULL;
}

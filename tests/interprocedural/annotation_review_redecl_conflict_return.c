#include <stdlib.h>

#define CAND_RETURNS_OWN __attribute__((annotate("cand:returns_own")))
#define CAND_RETURNS_BORROW_FROM_0 __attribute__((annotate("cand:returns_borrow_from:0")))

/* Conflicting declaration annotations across redeclarations of the same
 * external symbol: one header claims an owned return, another claims a
 * borrow. Clang inherits both onto the merged declaration; the conflict
 * can never match a single reviewed manifest entry, so it must fail
 * closed. */
extern CAND_RETURNS_OWN int *reviewed_conflicting(int *base);
extern CAND_RETURNS_BORROW_FROM_0 int *reviewed_conflicting(int *base);

int main(void)
{
    int storage = 1;
    int *p = reviewed_conflicting(&storage);
    return p != NULL;
}

#include <stdlib.h>

static int *owned_exact(void)
{
    return malloc(sizeof(int));
}

static int *owned_with_borrow(int *borrowed)
{
    if (borrowed) (void)*borrowed;
    return malloc(sizeof(int));
}

static void destroy_with_borrow(int *owned, int *borrowed)
{
    if (borrowed) (void)*borrowed;
    free(owned);
}

#define CAND_RETURNS_OWN __attribute__((annotate("cand:returns_own")))
static CAND_RETURNS_OWN int *annotated_owned(void)
{
    return malloc(sizeof(int));
}

int main(void)
{
    int *a = owned_exact();
    free(a);
    int *b = owned_with_borrow(NULL);
    free(b);
    int *owned = malloc(sizeof(int));
    int *borrowed = malloc(sizeof(int));
    destroy_with_borrow(owned, borrowed);
    free(borrowed);
    int *annotated = annotated_owned();
    free(annotated);
    return 0;
}

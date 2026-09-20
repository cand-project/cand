#include <stdlib.h>
#define CAND_A(value) __attribute__((annotate(value)))
#define CAND_BORROW CAND_A("cand:borrow_shared")

static int invalid(int *p CAND_BORROW) { free(p); return *p; }

int main(void)
{
    return invalid(malloc(sizeof(int)));
}

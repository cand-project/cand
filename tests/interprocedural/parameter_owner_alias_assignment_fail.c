#include <stdlib.h>
#define CAND_A(value) __attribute__((annotate(value)))
#define CAND_TAKES CAND_A("cand:takes")
#define CAND_MOVE(value) (value)

static int assigned_alias(int *p CAND_TAKES)
{
    int *q = NULL;
    q = p;
    free(p);
    return *q;
}

int main(void)
{
    return assigned_alias(CAND_MOVE(malloc(sizeof(int))));
}

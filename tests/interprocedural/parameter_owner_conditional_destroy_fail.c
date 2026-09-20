#include <stdlib.h>
#define CAND_A(value) __attribute__((annotate(value)))
#define CAND_TAKES CAND_A("cand:takes")
#define CAND_MOVE(value) (value)

static int conditional_destroy(int *p CAND_TAKES, int flag)
{
    if (flag) free(p);
    return *p;
}

int main(void)
{
    return conditional_destroy(CAND_MOVE(malloc(sizeof(int))), 1);
}

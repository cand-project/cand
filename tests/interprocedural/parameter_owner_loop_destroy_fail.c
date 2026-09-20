#include <stdlib.h>
#define CAND_A(value) __attribute__((annotate(value)))
#define CAND_TAKES CAND_A("cand:takes")
#define CAND_MOVE(value) (value)

static int loop_destroy(int *p CAND_TAKES, int count)
{
    while (count-- > 0) free(p);
    return *p;
}

int main(void)
{
    return loop_destroy(CAND_MOVE(malloc(sizeof(int))), 1);
}

#include <stdlib.h>
#define CAND_A(value) __attribute__((annotate(value)))
#define CAND_TAKES CAND_A("cand:takes")
#define CAND_MOVE(value) (value)

static int free_p_use_q(int *p CAND_TAKES)
{
    int *q = p;
    free(p);
    return *q;
}

static int free_q_use_p(int *p CAND_TAKES)
{
    int *q = p;
    free(q);
    return *p;
}

int main(void)
{
    int first = free_p_use_q(CAND_MOVE(malloc(sizeof(int))));
    int second = free_q_use_p(CAND_MOVE(malloc(sizeof(int))));
    return first + second;
}

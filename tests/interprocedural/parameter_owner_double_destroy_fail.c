#include <stdlib.h>
#define CAND_A(value) __attribute__((annotate(value)))
#define CAND_TAKES CAND_A("cand:takes")
#define CAND_MOVE(value) (value)

static void destroy_p_then_q(int *p CAND_TAKES)
{
    int *q = p;
    free(p);
    free(q);
}

static void destroy_q_then_p(int *p CAND_TAKES)
{
    int *q = p;
    free(q);
    free(p);
}

int main(void)
{
    destroy_p_then_q(CAND_MOVE(malloc(sizeof(int))));
    destroy_q_then_p(CAND_MOVE(malloc(sizeof(int))));
    return 0;
}

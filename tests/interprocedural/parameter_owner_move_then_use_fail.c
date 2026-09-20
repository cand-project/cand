#include <stdlib.h>
#define CAND_A(value) __attribute__((annotate(value)))
#define CAND_TAKES CAND_A("cand:takes")
#define CAND_MOVE(value) (value)

static void sink(int *p CAND_TAKES) { free(p); }

static int move_then_use(int *p CAND_TAKES)
{
    sink(CAND_MOVE(p));
    return *p;
}

int main(void)
{
    return move_then_use(CAND_MOVE(malloc(sizeof(int))));
}

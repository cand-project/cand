#include <stdlib.h>
#define CAND_A(value) __attribute__((annotate(value)))
#define CAND_TAKES CAND_A("cand:takes")

static int destroy_then_write(int *p CAND_TAKES) { free(p); *p = 1; return *p; }

int main(void)
{
    return destroy_then_write(malloc(sizeof(int)));
}

#include <stdlib.h>
#define CAND_A(value) __attribute__((annotate(value)))
#define CAND_TAKES CAND_A("cand:takes")
#define CAND_MOVE(value) (value)

static int read_owner(int *p CAND_TAKES) { return *p; }

int main(void)
{
    int *p = malloc(sizeof *p);
    if (!p) return 0;
    *p = 1;
    return read_owner(CAND_MOVE(p));
}

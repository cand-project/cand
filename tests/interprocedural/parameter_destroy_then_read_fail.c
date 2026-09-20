#include <stdlib.h>
#define CAND_A(value) __attribute__((annotate(value)))
#define CAND_DESTROYS CAND_A("cand:destroys")

static int destroy_then_read(int *p CAND_DESTROYS) { free(p); return *p; }

int main(void)
{
    return destroy_then_read(malloc(sizeof(int)));
}

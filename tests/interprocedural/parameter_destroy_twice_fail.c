#include <stdlib.h>
#define CAND_A(value) __attribute__((annotate(value)))
#define CAND_DESTROYS CAND_A("cand:destroys")

static void destroy_twice(int *p CAND_DESTROYS) { free(p); free(p); }

int main(void)
{
    destroy_twice(malloc(sizeof(int)));
    return 0;
}

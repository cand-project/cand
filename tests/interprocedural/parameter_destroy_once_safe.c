#include <stdlib.h>
#define CAND_A(value) __attribute__((annotate(value)))
#define CAND_DESTROYS CAND_A("cand:destroys")

static void destroy_once(int *p CAND_DESTROYS) { free(p); }

int main(void)
{
    int *p = malloc(sizeof *p);
    destroy_once(p);
    return 0;
}

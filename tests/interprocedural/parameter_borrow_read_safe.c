#include <stdlib.h>
#define CAND_A(value) __attribute__((annotate(value)))
#define CAND_BORROW CAND_A("cand:borrow_shared")

static int read_value(const int *p CAND_BORROW) { return *p; }

int main(void)
{
    int *p = malloc(sizeof *p);
    if (!p) return 0;
    *p = 1;
    int result = read_value(p);
    free(p);
    return result;
}

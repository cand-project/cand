#include <stdlib.h>

struct Item {
    int m;
};

/*
 * Independent-review regression: arrow member access on a pointer-arithmetic
 * base after destruction.
 *
 * Expected: FAIL CAND-T002, ASan violation.
 */
int main(void)
{
    struct Item *items = malloc(2 * sizeof *items);
    if (items == NULL) {
        return 2;
    }
    items[0].m = 1;
    items[1].m = 2;
    free(items);
    return (items + 1)->m;
}

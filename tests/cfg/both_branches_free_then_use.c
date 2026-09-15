#include <stdlib.h>

/*
 * P0.2 (spec §9E): both branches destroy, then the code accesses the object.
 * After the join the object is Dead on every path.
 *
 * Expected: FAIL CAND-T002 (definite).
 */
int f(int flag)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    if (flag) {
        free(p);
    } else {
        free(p);
    }

    return *p;
}

int main(void)
{
    return f(1);
}

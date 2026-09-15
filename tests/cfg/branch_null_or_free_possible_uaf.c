#include <stdlib.h>

/*
 * P0.2: on the path where `p` is set to NULL the later access is a null
 * dereference (not a lifetime issue), but on the other path the object was
 * destroyed. The join of Null and Dead yields MaybeDead, so the access is a
 * possible use-after-free -- which is exactly what the runtime path
 * exercised by main triggers.
 *
 * Expected: FAIL CAND-T002 (certainty possible), ASan violation.
 */
int f(int c)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    if (c) {
        free(p);
    } else {
        p = 0;
    }

    return *p;
}

int main(void)
{
    return f(1);
}

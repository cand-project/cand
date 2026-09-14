#include <stdlib.h>

/*
 * Positive differential control: a fully modeled P0 lifecycle.
 * Ordinary compilers accept it, ASan is silent, cand returns PASS.
 */
int main(void)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    *p = 42;
    int result = *p;
    free(p);
    free(NULL);

    return result == 42 ? 0 : 1;
}

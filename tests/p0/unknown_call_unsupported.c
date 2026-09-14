#include <stdlib.h>

static void observe(int *p)
{
    (void)p;
}

int main(void)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    observe(p);
    free(p);
    return 0;
}

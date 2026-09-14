#include <stdlib.h>

int main(void)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    int *q = p;
    *q = 7;
    free(p);
    return 0;
}

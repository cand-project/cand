#include <stdlib.h>

int main(void)
{
    void *p = malloc(8);
    if (p == NULL) {
        return 2;
    }

    free(p);
    return 0;
}

#include <stdlib.h>

int main(void)
{
    int *item = malloc(sizeof *item);
    if (!item) return 0;
    free(item);
    return *item;
}

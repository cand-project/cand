#include <stdlib.h>

int main(void)
{
    int *item = malloc(sizeof *item);
    if (!item) return 0;
    *item = 17;
    int result = *item;
    free(item);
    return result == 17 ? 0 : 1;
}

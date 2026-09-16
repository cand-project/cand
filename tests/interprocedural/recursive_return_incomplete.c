#include <stdlib.h>

static int *recursive_make(unsigned depth)
{
    if (depth == 0u) return malloc(sizeof(int));
    return recursive_make(depth - 1u);
}

int main(void)
{
    int *value = recursive_make(2u);
    free(value);
    return 0;
}

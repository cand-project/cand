#include <stdlib.h>

static int *make_right(unsigned depth);

static int *make_left(unsigned depth)
{
    if (depth == 0u) return malloc(sizeof(int));
    return make_right(depth - 1u);
}

static int *make_right(unsigned depth)
{
    return make_left(depth);
}

int main(void)
{
    int *value = make_left(1u);
    free(value);
    return 0;
}

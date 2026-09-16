#include <stdlib.h>

static void release_value(int *value)
{
    free(value);
}

int main(void)
{
    int *value = malloc(sizeof *value);
    if (value == NULL) return 0;
    release_value(value);
    release_value(value);
    return 0;
}

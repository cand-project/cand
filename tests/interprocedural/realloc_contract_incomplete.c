#include <stdlib.h>

static int *resize_value(int *value)
{
    return realloc(value, sizeof *value * 2u);
}

int main(void)
{
    int *value = malloc(sizeof *value);
    if (value == NULL) return 0;
    value = resize_value(value);
    free(value);
    return 0;
}

#include <stdlib.h>

static int *saved_value;

static void retain_value(int *value)
{
    saved_value = value;
}

int main(void)
{
    int *value = malloc(sizeof *value);
    if (value == NULL) return 0;
    retain_value(value);
    free(value);
    return *saved_value;
}

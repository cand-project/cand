#include <stdlib.h>

static int *raw_make(void)
{
    return malloc(sizeof(int));
}

static int *make_value(void)
{
    return raw_make();
}

int main(void)
{
    int *value = make_value();
    if (value == NULL) return 0;
    *value = 19;
    free(value);
    return 0;
}

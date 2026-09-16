#include <stdlib.h>

static int *identity_view(int *value)
{
    return value;
}

int main(void)
{
    int *owner = malloc(sizeof *owner);
    if (owner == NULL) return 0;
    int *view = identity_view(owner);
    *view = 23;
    free(owner);
    return 0;
}

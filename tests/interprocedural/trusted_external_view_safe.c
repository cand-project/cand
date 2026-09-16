#include <stdlib.h>

extern int *vendor_view(int *value);

int main(void)
{
    int *owner = malloc(sizeof *owner);
    if (owner == NULL) return 0;
    int *view = vendor_view(owner);
    *view = 29;
    free(owner);
    return 0;
}

#include <stdlib.h>

extern int *vendor_create(void);

int main(void)
{
    int *item = vendor_create();
    if (!item) return 0;
    *item = 23;
    free(item);
    return 0;
}

#include <stdlib.h>

extern void vendor_touch(int *p);

int main(void)
{
    int *value = malloc(sizeof *value);
    if (value == NULL) return 0;
    vendor_touch(value);
    free(value);
    return 0;
}

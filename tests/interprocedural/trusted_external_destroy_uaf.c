#include <stdlib.h>

extern void vendor_destroy(int *value);

int main(void)
{
    int *value = malloc(sizeof *value);
    if (value == NULL) return 0;
    vendor_destroy(value);
    return *value;
}

#include <stdlib.h>

extern int *vendor_create(void);

static int *create_packet(void)
{
    return vendor_create();
}

int main(void)
{
    int *packet = create_packet();
    free(packet);
    return 0;
}

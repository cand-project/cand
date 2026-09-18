#include <stdlib.h>
#include "p2_compat.h"

typedef struct Header { int type; } Header;
typedef Header Packet;

CAND_RETURNS_OWN Packet *packet_new(void)
{
    return malloc(sizeof(Packet));
}

CAND_RETURNS_BORROW_FROM(0) Header *packet_header(Packet *packet)
{
    return packet;
}

CAND_RETURNS_BORROW_FROM(0) Header *get_header(Packet *packet)
{
    return packet_header(packet);
}

int main(void)
{
    Packet *packet CAND_OWN = packet_new();
    Header *header CAND_BORROW = get_header(packet);
    int result = header->type;
    free(packet);
    return result;
}

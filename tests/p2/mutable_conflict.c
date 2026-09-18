#include <stdlib.h>
#include "p2_compat.h"

typedef struct Header { int type; } Header;
typedef Header Packet;

CAND_RETURNS_OWN Packet *packet_new(void)
{
    return malloc(sizeof(Packet));
}

CAND_RETURNS_BORROW_FROM(0) Header *packet_header_mut(Packet *packet)
{
    return packet;
}

int main(void)
{
    Packet *packet CAND_OWN = packet_new();
    Header *first CAND_BORROW_MUT = packet_header_mut(packet);
    Header *second CAND_BORROW_MUT = packet_header_mut(packet);
    first->type = second->type;
    free(packet);
    return first->type;
}

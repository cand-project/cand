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

Header *bad_view(void)
{
    Packet *packet CAND_OWN = packet_new();
    Header *header CAND_BORROW = packet_header(packet);
    return header;
}

int main(void)
{
    Header *header = bad_view();
    return header->type;
}

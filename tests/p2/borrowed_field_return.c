#include <stdlib.h>
#include "p2_compat.h"

typedef struct Header { int type; } Header;
typedef struct Packet { Header header; } Packet;

CAND_RETURNS_OWN Packet *packet_new(void)
{
    Packet *packet = malloc(sizeof(*packet));
    if (packet != NULL) packet->header.type = 7;
    return packet;
}

CAND_RETURNS_BORROW_FROM(0) Header *packet_header(Packet *packet)
{
    return &packet->header;
}

int main(void)
{
    Packet *packet CAND_OWN = packet_new();
    Header *header CAND_BORROW = packet_header(packet);
    int result = header->type;

    free(packet);
    return result == 7 ? 0 : 1;
}

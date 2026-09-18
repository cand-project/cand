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

static int inspect(Header *header)
{
    return header->type;
}

int main(void)
{
    Packet *packet CAND_OWN = packet_new();
    Header *header CAND_BORROW = packet_header(packet);
    free(packet);
    return inspect(header);
}

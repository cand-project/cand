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

int main(void)
{
    Packet *packet CAND_OWN = packet_new();
    Header *h1 CAND_BORROW = packet_header(packet);
    Header *h2 CAND_BORROW = packet_header(packet);
    int result = h1->type + h2->type;
    free(packet);
    return result;
}

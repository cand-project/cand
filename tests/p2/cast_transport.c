#include <stdlib.h>
#include "p2_compat.h"

typedef struct Header { int type; } Header;
typedef Header Packet;

CAND_RETURNS_OWN Packet *packet_new(void) { return malloc(sizeof(Packet)); }
CAND_RETURNS_BORROW_FROM(0) Header *packet_header(Packet *packet) { return packet; }

int main(void)
{
    Packet *packet CAND_OWN = packet_new();
    Header *header CAND_BORROW = packet_header(packet);
    void *opaque = (void *)header;
    Header *again = (Header *)opaque;
    free(packet);
    return again->type;
}

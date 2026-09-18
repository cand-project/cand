#include <stdlib.h>
#include "p2_compat.h"

typedef struct Header { int type; } Header;
typedef Header Packet;

CAND_RETURNS_OWN Packet *packet_new(void) { return malloc(sizeof(Packet)); }
CAND_RETURNS_BORROW_FROM(0) Header *packet_header_mut(Packet *packet) { return packet; }
extern void packet_modify(Packet *);

int main(void)
{
    Packet *packet CAND_OWN = packet_new();
    Header *header CAND_BORROW_MUT = packet_header_mut(packet);
    packet_modify(packet);
    free(packet);
    return header->type;
}

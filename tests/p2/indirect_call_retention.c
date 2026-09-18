#include <stdlib.h>
#include "p2_compat.h"

typedef struct Header { int type; } Header;
typedef Header Packet;
typedef void (*HeaderConsumer)(Header *);

CAND_RETURNS_OWN Packet *packet_new(void) { return malloc(sizeof(Packet)); }
CAND_RETURNS_BORROW_FROM(0) Header *packet_header(Packet *packet) { return packet; }
extern void retain_unknown(Header *);

int main(void)
{
    Packet *packet CAND_OWN = packet_new();
    Header *header CAND_BORROW = packet_header(packet);
    HeaderConsumer consumer = retain_unknown;
    consumer(header);
    free(packet);
    return 0;
}

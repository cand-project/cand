#include <stdlib.h>
#include "p2_compat.h"

typedef struct Header { int type; } Header;
typedef Header Packet;

CAND_RETURNS_OWN Packet *packet_new(void) { return malloc(sizeof(Packet)); }
CAND_RETURNS_BORROW_FROM(0) Header *packet_header(Packet *packet) { return packet; }
void inspect(Header *header CAND_BORROW) { (void)header->type; }

int main(int argc, char **argv)
{
    (void)argv;
    Packet *packet CAND_OWN = packet_new();
    Header *header CAND_BORROW = packet_header(packet);
    switch (argc) {
    case 1:
        inspect(header);
        return 0;
    case 2:
        free(packet);
        return 0;
    default:
        free(packet);
        return 0;
    }
}

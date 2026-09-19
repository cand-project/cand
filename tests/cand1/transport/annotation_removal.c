#include <cand/cand.h>
#include <stdlib.h>

/* Removing CAND_BORROW must not silently erase a verified parent relationship. */
typedef struct Header { int value; } Header;
typedef Header Packet;
CAND_RETURNS_OWN Packet *packet_new(void) { return malloc(sizeof(Packet)); }
CAND_RETURNS_BORROW_FROM(0) Header *packet_header(Packet *packet) { return packet; }

int annotation_removed(void)
{
    Packet *p CAND_OWN = packet_new();
    Header *view = packet_header(p);
    free(p);
    return view->value;
}

#include <stdlib.h>
#include <cand/cand.h>

typedef struct Packet { int value; } Packet;
typedef struct Holder { Packet *packet; } Holder;

CAND_RETURNS_OWN Packet *packet_new(void)
{
    return malloc(sizeof(Packet));
}

int main(void)
{
    Packet *packet CAND_OWN = packet_new();
    Holder holder;
    holder.packet = CAND_MOVE(packet);
    (void)holder;
    return packet->value;
}

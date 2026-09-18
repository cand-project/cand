#include <stdlib.h>
#include <cand/cand.h>

typedef struct Packet { int value; } Packet;

CAND_RETURNS_OWN Packet *packet_new(void)
{
    return malloc(sizeof(Packet));
}

int main(void)
{
    Packet *packet CAND_OWN = packet_new();
    packet = CAND_MOVE(packet);
    return packet->value;
}

#include <stdlib.h>
#include <cand/cand.h>

typedef struct Packet { int value; } Packet;

CAND_RETURNS_OWN Packet *packet_new(void)
{
    return malloc(sizeof(Packet));
}

void consume(Packet *packet CAND_TAKES)
{
    (void)packet;
}

#define TAKE_OWNERSHIP(value) consume(CAND_MOVE(value))

int main(void)
{
    Packet *packet CAND_OWN = packet_new();
    TAKE_OWNERSHIP(packet);
    return packet->value;
}

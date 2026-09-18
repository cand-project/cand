#include <stdlib.h>
#include <cand/cand.h>

typedef struct Packet { int value; } Packet;

CAND_RETURNS_OWN Packet *packet_new(void)
{
    return malloc(sizeof(Packet));
}

void packet_free(Packet *packet CAND_DESTROYS)
{
    free(packet);
}

int main(void)
{
    Packet *packet CAND_OWN = packet_new();
    Packet *owned CAND_OWN = CAND_MOVE(packet);
    packet_free(owned);
    return 0;
}

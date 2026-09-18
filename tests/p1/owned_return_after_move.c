#include <stdlib.h>
#include <cand/cand.h>

typedef struct Packet { int value; } Packet;

CAND_RETURNS_OWN Packet *identity(Packet *packet CAND_TAKES)
{
    return packet;
}

void packet_free(Packet *packet CAND_DESTROYS)
{
    free(packet);
}

int main(void)
{
    Packet *packet CAND_OWN = malloc(sizeof(Packet));
    Packet *owned CAND_OWN = identity(CAND_MOVE(packet));
    packet_free(owned);
    return 0;
}

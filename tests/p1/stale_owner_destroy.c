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

void packet_free(Packet *packet CAND_DESTROYS)
{
    free(packet);
}

int main(void)
{
    Packet *packet CAND_OWN = packet_new();
    consume(CAND_MOVE(packet));
    packet_free(packet);
    return 0;
}

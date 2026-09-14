#include <stdlib.h>
#include <cand/cand.h>

typedef struct Packet {
    size_t len;
} Packet;

Packet *packet_new(void) CAND_RETURNS_OWN;
void packet_free(Packet *packet CAND_DESTROYS);
void packet_send(Packet *packet CAND_TAKES);
void example(void) CAND_SAFE;

Packet *packet_new(void)
{
    return malloc(sizeof(Packet));
}

void packet_free(Packet *packet)
{
    free(packet);
}

void packet_send(Packet *packet)
{
    packet_free(packet);
}

void example(void)
{
    Packet *packet CAND_OWN = packet_new();
    packet_send(CAND_MOVE(packet));

#if defined(CAND_NEGATIVE_EXAMPLE)
    packet->len = 1;
#endif
}

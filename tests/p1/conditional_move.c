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

int main(int argc, char **argv)
{
    (void)argv;
    int flag = argc;
    Packet *packet CAND_OWN = packet_new();
    if (flag) consume(CAND_MOVE(packet));
    return packet->value;
}

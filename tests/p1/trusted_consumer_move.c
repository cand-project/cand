#include <stdlib.h>
#include <cand/cand.h>

typedef struct Packet { int value; } Packet;
extern void queue_push(Packet *packet);

int main(void)
{
    Packet *packet CAND_OWN = malloc(sizeof(Packet));
    queue_push(CAND_MOVE(packet));
    return 0;
}

#include <stdlib.h>

#define CAND_TAKES __attribute__((annotate("cand:takes")))

/* Access after ownership was transferred to the reviewed external
 * consumer. */
extern void reviewed_queue_push(int *item CAND_TAKES);

int main(void)
{
    int *item = malloc(sizeof *item);
    if (item == NULL) return 0;
    reviewed_queue_push(item);
    return *item;
}

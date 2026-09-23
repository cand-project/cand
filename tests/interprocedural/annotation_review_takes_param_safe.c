#include <stdlib.h>

#define CAND_TAKES __attribute__((annotate("cand:takes")))

/* Reviewed consumes-parameter boundary: ownership transfers to the
 * external callee. The caller does not access or destroy it. */
extern void reviewed_queue_push(int *item CAND_TAKES);

int main(void)
{
    int *item = malloc(sizeof *item);
    if (item == NULL) return 0;
    *item = 42;
    reviewed_queue_push(item);
    return 0;
}

#include <stdlib.h>

extern void queue_push(int *p);

int main(void)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) return 0;

    *p = 42;
    queue_push(p);

    /* The queue owns p now. The caller does not access or destroy it. */
    return 0;
}

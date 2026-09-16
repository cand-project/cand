#include <stdlib.h>

extern void queue_push(int *value);

static void enqueue_value(int *value)
{
    queue_push(value);
}

int main(void)
{
    int *value = malloc(sizeof *value);
    if (value == NULL) return 0;
    enqueue_value(value);
    free(value);
    return 0;
}

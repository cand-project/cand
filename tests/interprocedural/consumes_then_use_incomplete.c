#include <stdlib.h>

extern void queue_push(int *p);

int main(void)
{
    int *value = malloc(sizeof *value);
    if (value == NULL) return 0;
    queue_push(value);
    return *value;
}

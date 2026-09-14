#include <stdlib.h>

int main(void)
{
    int *tail = calloc(4, sizeof(*tail));
    if (tail == NULL) {
        return 2;
    }

    tail[0] = 7;
    tail[1] = 8;

    /* The snake round ends and its tail storage is released. */
    free(tail);

    /* Score code still reads a segment from dead storage. */
    return tail[0] == 7 ? 0 : 1;
}

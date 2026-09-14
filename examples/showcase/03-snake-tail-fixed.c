#include <stdlib.h>

int main(void)
{
    int *tail = calloc(4, sizeof(*tail));
    if (tail == NULL) {
        return 2;
    }

    tail[0] = 7;
    tail[1] = 8;

    /* Copy the score-relevant value while the object is alive. */
    const int first_segment = tail[0];
    free(tail);

    return first_segment == 7 ? 0 : 1;
}

#include <stdlib.h>

typedef struct Treasure {
    int gold;
} Treasure;

int main(void)
{
    Treasure *chest = calloc(1, sizeof(*chest));
    if (chest == NULL) {
        return 2;
    }

    chest->gold = 250;

    /* Player leaves the room: the chest is destroyed. */
    free(chest);

    /* Cleanup runs twice after a generated refactor. */
    free(chest);

    return 0;
}

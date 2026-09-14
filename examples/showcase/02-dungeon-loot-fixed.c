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

    /* Exactly one owner performs exactly one destruction. */
    free(chest);

    return 0;
}

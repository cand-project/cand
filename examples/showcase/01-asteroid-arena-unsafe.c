#include <stdlib.h>

typedef struct Asteroid {
    int hull;
    int bounty;
} Asteroid;

int main(void)
{
    Asteroid *boss = malloc(sizeof(*boss));
    if (boss == NULL) {
        return 2;
    }

    boss->hull = 0;
    boss->bounty = 9000;

    /* The boss object is gone after this point. */
    free(boss);

    /* Ordinary C accepts this. C& must reject the dead-object access. */
    return boss->bounty == 9000 ? 0 : 1;
}

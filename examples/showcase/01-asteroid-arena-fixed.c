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

    /* Keep the value, not a dead pointer. */
    const int bounty = boss->bounty;
    free(boss);

    return bounty == 9000 ? 0 : 1;
}

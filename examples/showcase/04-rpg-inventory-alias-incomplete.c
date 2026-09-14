#include <stdlib.h>

typedef struct Weapon {
    int damage;
} Weapon;

int main(void)
{
    Weapon *weapon = malloc(sizeof(*weapon));
    if (weapon == NULL) {
        return 2;
    }

    weapon->damage = 42;

    /* Two names now refer to one allocation. Who owns it? Who only borrows? */
    Weapon *equipped = weapon;

    free(weapon);

    /* P0 intentionally refuses to claim this alias relationship is safe. */
    return equipped->damage == 42 ? 0 : 1;
}

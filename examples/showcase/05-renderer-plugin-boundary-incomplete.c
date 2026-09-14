#include <stdlib.h>

typedef struct Sprite {
    int texture_id;
} Sprite;

static void renderer_register(Sprite *sprite)
{
    /* Imagine this is a third-party renderer API. */
    (void)sprite;
}

int main(void)
{
    Sprite *hero = malloc(sizeof(*hero));
    if (hero == NULL) {
        return 2;
    }

    hero->texture_id = 17;

    /* Does renderer_register borrow, retain, consume, or destroy hero? */
    renderer_register(hero);

    free(hero);
    return 0;
}

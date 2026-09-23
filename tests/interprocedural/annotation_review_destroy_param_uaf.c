#include <stdlib.h>

#define CAND_DESTROYS __attribute__((annotate("cand:destroys")))

/* Use after the reviewed external destructor consumed the object. */
extern void reviewed_destroy(int *value CAND_DESTROYS);

int main(void)
{
    int *value = malloc(sizeof *value);
    if (value == NULL) return 0;
    reviewed_destroy(value);
    return *value;
}

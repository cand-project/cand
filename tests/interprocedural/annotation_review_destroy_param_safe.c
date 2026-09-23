#include <stdlib.h>

#define CAND_DESTROYS __attribute__((annotate("cand:destroys")))

/* Reviewed destroys-parameter boundary: the external callee takes
 * ownership and destroys the argument. The caller does not touch it
 * afterwards. */
extern void reviewed_destroy(int *value CAND_DESTROYS);

int main(void)
{
    int *value = malloc(sizeof *value);
    if (value == NULL) return 0;
    reviewed_destroy(value);
    return 0;
}

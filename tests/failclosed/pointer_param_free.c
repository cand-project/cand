#include <stdlib.h>

/*
 * P0.4 derives destroys(arg0) from this visible wrapper and applies it to the
 * caller's ObjectId.
 */
void destroy(int *p)
{
    free(p);
}

int main(void)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }
    destroy(p);
    return 0;
}

#include <stdlib.h>

/*
 * GNU statement expressions embed control flow that the linear P0 analyzer
 * would flatten unsoundly. Both statement-position and expression-position
 * statement expressions must be rejected.
 *
 * Required P0.1 result: INCOMPLETE (statement-expression).
 */
int main(void)
{
    int *p = malloc(sizeof *p);
    if (p == NULL) {
        return 2;
    }

    *p = ({
        1;
    });

    free(p);
    return 0;
}

/* #41 Gate B: realloc-like name guard -- in-place production is a
 * different (unsupported) transport. */
#include <stdlib.h>
extern int widget_realloc(int **out);
int main(void) {
    int *out = NULL;
    widget_realloc(&out);
    *out = 1;
    free(out);
    return 0;
}

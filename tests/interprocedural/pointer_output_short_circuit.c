/* #41: two produces short-circuited in one || condition. Clang
 * materializes each operand's call as an element of its own block but
 * gives every chain block the full condition as its terminator; the
 * first block's defensive terminator walk must not pre-apply the second
 * block's call (the second call would then see its own destination as
 * live and be spuriously refused). Both calls convert: write:always +
 * nullable:false is immediately usable, so there are no rows. */
#include <stdlib.h>
extern int po_always(int **out);
int main(void) {
    int *a = NULL;
    int *b = NULL;
    if (po_always(&a) != 0 || po_always(&b) != 0)
        return 1;
    *a = 1;
    *b = 2;
    free(a);
    free(b);
    return 0;
}

/* #41 Gate B [F8]: use on the failure edge -- the binding keeps the
 * pre-call Null relation and carries the produced-maybe mark; the
 * mark is tested BEFORE the relation-silence paths. */
#include <stdlib.h>
extern int po_zero(int **out);
int main(void) {
    int *out = NULL;
    if (po_zero(&out) != 0) {
        return *out;
    }
    return 0;
}

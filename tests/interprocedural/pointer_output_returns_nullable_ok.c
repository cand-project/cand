/* #41 Gate B: positive control for the legacy returns-level
 * `nullable:` key -- ignored semantically for non-produces symbols, so
 * the bundle must keep loading under the v2 feature. */
#include <stdlib.h>
extern int *po_owned_maybe(void);
int main(void) {
    int *p = po_owned_maybe();
    if (p) {
        *p = 1;
        free(p);
    }
    return 0;
}

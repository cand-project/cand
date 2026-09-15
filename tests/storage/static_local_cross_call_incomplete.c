#include <stdlib.h>
static int use_slot(int destroy) {
    static int *p;
    if (!p) { p = malloc(sizeof *p); *p = 1; }
    if (destroy) free(p);
    return p ? *p : 0;
}
int main(void) { (void)use_slot(1); return use_slot(0); }

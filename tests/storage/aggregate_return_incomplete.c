#include <stdlib.h>
struct S { int *p; };
static struct S make(void) {
    struct S s = {0};
    s.p = malloc(sizeof *s.p);
    return s;
}
int main(void) { struct S s = make(); free(s.p); return 0; }

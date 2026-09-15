#include <stdlib.h>
struct S { int *p; };
int main(void) { struct S s = {0}; s.p = malloc(sizeof *s.p); int *q = s.p; free(s.p); return *q; }

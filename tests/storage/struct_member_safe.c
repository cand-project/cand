#include <stdlib.h>
struct S { int *p; };
int main(void) { struct S s = {0}; s.p = malloc(sizeof *s.p); *s.p = 5; free(s.p); return 0; }

#include <stdlib.h>
int main(void) { int *p = malloc(sizeof *p); int *q = p; for (int i = 0; i < 1; ++i) free(p); return *q; }

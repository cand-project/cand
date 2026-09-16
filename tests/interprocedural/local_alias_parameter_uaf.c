#include <stdlib.h>
static int read_alias(const int *p) { const int *q = p; return *q; }
int main(void) { int *p = malloc(sizeof *p); free(p); return read_alias(p); }

#include <stdlib.h>
static int *identity(int *p) { return p; }
static int *select_second(int *p, int *q) { return identity(q); }
int main(void) { int *p = malloc(sizeof *p); int *q = malloc(sizeof *q); int *r = select_second(p, q); free(q); free(p); return *r; }

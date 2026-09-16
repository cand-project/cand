#include <stdlib.h>
static int *identity_view(int *p) { return p; }
int main(void) { int *p = malloc(sizeof *p); int *q = identity_view(p); free(p); return *q; }

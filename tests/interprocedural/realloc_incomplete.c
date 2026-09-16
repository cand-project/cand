#include <stdlib.h>
static int *resize(int *p) { return realloc(p, 8); }
int main(void) { int *p = malloc(4); p = resize(p); free(p); return 0; }

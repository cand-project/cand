#include <stdlib.h>
static int read_at(const int *p) { return p[0]; }
int main(void) { int *p = malloc(sizeof *p); free(p); return read_at(p); }

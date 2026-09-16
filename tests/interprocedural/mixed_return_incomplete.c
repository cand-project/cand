#include <stdlib.h>
static int *maybe_make(int *existing, int flag) { if (flag) return malloc(sizeof(int)); return existing; }
int main(void) { int *p = malloc(sizeof *p); int *q = maybe_make(p, 0); free(p); return *q; }

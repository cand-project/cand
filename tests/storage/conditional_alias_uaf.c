#include <stdlib.h>
int main(int flag) { int *p = malloc(sizeof *p); int *q = 0; if (flag) q = p; if (q) free(q); return *p; }

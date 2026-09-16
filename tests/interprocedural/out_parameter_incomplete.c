#include <stdlib.h>
static void make_out(int **out) { *out = malloc(sizeof **out); }
int main(void) { int *p = NULL; make_out(&p); free(p); return 0; }

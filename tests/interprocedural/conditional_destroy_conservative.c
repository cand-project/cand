#include <stdlib.h>
static void maybe_destroy(int *p, int flag) { if (flag) free(p); }
int main(void) { int *p = malloc(sizeof *p); maybe_destroy(p, 0); free(p); return 0; }

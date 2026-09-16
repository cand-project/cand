#include <stdlib.h>
static void destroy_first(int *a, int *b) { (void)b; free(a); }
static void wrapper(int *p, int *q) { destroy_first(q, p); }
int main(void) { int *p = malloc(sizeof *p); int *q = malloc(sizeof *q); wrapper(p, q); free(p); return *q; }

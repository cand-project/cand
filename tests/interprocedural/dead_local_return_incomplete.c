#include <stdlib.h>
static int *bad_make(void) { int *p = malloc(sizeof *p); free(p); return p; }
int main(void) { int *p = bad_make(); return *p; }

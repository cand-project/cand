#include <stdlib.h>
int main(void) { int *p = malloc(sizeof *p); int *q = p; free(p); return *q; }

#include <stdlib.h>
int main(void) { int *p = malloc(sizeof *p); int *q = 0; q = p; free(q); return 0; }

#include <stdlib.h>
int main(void) { int *p = malloc(sizeof *p); int *q = p; q = 0; free(p); return 0; }

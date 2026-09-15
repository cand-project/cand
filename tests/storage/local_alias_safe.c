#include <stdlib.h>
int main(void) { int *p = malloc(sizeof *p); int *q = p; *q = 1; free(p); return 0; }

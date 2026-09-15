#include <stdlib.h>
int main(void) { int *p = malloc(2 * sizeof *p); int *q = p; free(p); return *(q + 1); }

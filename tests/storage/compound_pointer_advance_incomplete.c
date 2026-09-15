#include <stdlib.h>
int main(void) { int *p = malloc(2 * sizeof *p); p += 1; free(p); return 0; }

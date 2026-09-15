#include <stdlib.h>
int main(void) { int *p = malloc(2 * sizeof *p); ++p; free(p); return 0; }

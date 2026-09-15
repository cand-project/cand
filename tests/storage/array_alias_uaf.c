#include <stdlib.h>
int main(void) { int *items[4] = {0}; items[0] = malloc(sizeof *items[0]); int *q = items[0]; free(items[0]); return *q; }

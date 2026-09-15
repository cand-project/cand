#include <stdlib.h>
int main(void) { int *items[2] = {0}; items[0] = malloc(sizeof *items[0]); items[1] = malloc(sizeof *items[1]); free(items[0]); *items[1] = 1; free(items[1]); return 0; }

#include <stdlib.h>
int main(int i) { int *items[4] = {0}; items[i] = malloc(sizeof *items[i]); free(items[i]); return 0; }

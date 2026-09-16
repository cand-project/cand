#include <stdlib.h>
static int read_value(const int *p) { return *p; }
int main(void) { int *p = malloc(sizeof *p); *p = 7; int value = read_value(p); free(p); return value; }

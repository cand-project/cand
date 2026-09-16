#include <stdlib.h>
static void destroy_value(int *p) { free(p); }
int main(void) { int *p = malloc(sizeof *p); destroy_value(p); return *p; }

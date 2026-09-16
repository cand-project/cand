#include <stdlib.h>
static int *make_value(void) { int *p = malloc(sizeof *p); return p; }
int main(void) { int *p = make_value(); *p = 42; free(p); return 0; }

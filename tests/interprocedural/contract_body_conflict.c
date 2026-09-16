#include <stdlib.h>
static void bad_destroy(int *p) { free(p); }
int main(void) { int *p = malloc(sizeof *p); bad_destroy(p); return 0; }

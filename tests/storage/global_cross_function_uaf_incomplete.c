#include <stdlib.h>
static int *g;
static void setup(void) { g = malloc(sizeof *g); *g = 1; free(g); }
int main(void) { setup(); return *g; }

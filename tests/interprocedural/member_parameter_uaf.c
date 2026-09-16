#include <stdlib.h>
struct Item { int value; };
static int read_value(const struct Item *p) { return p->value; }
int main(void) { struct Item *p = malloc(sizeof *p); free(p); return read_value(p); }

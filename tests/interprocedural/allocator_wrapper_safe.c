#include <stdlib.h>
static int *make_value(void) { return malloc(sizeof(int)); }
int main(void) { int *p = make_value(); *p = 42; free(p); return 0; }

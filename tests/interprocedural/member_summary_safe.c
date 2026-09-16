#include <stdlib.h>
struct Item { int *value; };
static int *make_value(void) { return malloc(sizeof(int)); }
int main(void) { struct Item item = {0}; item.value = make_value(); *item.value = 7; free(item.value); return 0; }

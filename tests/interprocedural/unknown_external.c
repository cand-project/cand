#include <stdlib.h>
extern int *vendor_create(void);
int main(void) { int *p = vendor_create(); free(p); return 0; }

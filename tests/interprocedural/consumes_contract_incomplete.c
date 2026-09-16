#include <stdlib.h>
extern void queue_push(int *p);
int main(void) { int *p = malloc(sizeof *p); queue_push(p); free(p); return 0; }

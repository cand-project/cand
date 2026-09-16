#include <stdlib.h>
static void destroy_one(int *p) { free(p); }
static void maybe_destroy(int *a, int *b, int flag) { destroy_one(flag ? a : b); }
int main(void) { int *a = malloc(sizeof *a); int *b = malloc(sizeof *b); maybe_destroy(a, b, 1); free(a); return *b; }

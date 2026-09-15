#include <stdlib.h>
int main(int flag) {
    int *p = malloc(sizeof *p);
    int *q = p;
    *p = 1;
    if (flag) { free(q); q = NULL; }
    if (q) *q = 2;
    if (!flag) free(p);
    return 0;
}

#include <stdlib.h>
int main(void) {
    int *q = NULL;
    int sum = 0;
    for (int i = 0; i < 2; ++i) {
        if (q) sum += *q;
        int *p = malloc(sizeof *p);
        *p = i;
        q = p;
        free(p);
    }
    return sum;
}

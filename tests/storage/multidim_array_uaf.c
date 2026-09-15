#include <stdlib.h>
int main(void) {
    int *a[2][2] = {{0}};
    a[0][0] = malloc(sizeof *a[0][0]);
    a[1][0] = malloc(sizeof *a[1][0]);
    free(a[0][0]);
    int value = *a[0][0];
    free(a[1][0]);
    return value;
}

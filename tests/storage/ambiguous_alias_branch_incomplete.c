#include <stdlib.h>
int main(int argc, char **argv) {
    (void)argv;
    int *a = malloc(sizeof *a);
    int *b = malloc(sizeof *b);
    int *q;
    if (argc > 1) q = a; else q = b;
    free(a);
    int value = *q;
    free(b);
    return value;
}

#include <stdlib.h>
#include <string.h>
struct S { int *p; };
int main(void) {
    struct S a = {0}, b = {0};
    a.p = malloc(sizeof *a.p);
    *a.p = 9;
    memcpy(&b, &a, sizeof a);
    free(a.p);
    return *b.p;
}

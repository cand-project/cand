#include <setjmp.h>
#include <stdlib.h>
static jmp_buf env;
int main(void) {
    int *p = malloc(sizeof *p);
    if (setjmp(env) == 0) { free(p); longjmp(env, 1); }
    return 0;
}

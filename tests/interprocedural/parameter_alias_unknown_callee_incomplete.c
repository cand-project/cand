/*
 * Milestone #54 / ADR-0028 consuming-effect restriction: an unknown
 * callee receiving the alias is NOT resolved to a parameter effect;
 * the existing unknown-call-with-tracked-pointer escape obligation is
 * unchanged.
 */
#include <stdlib.h>
extern void sink(int *p);
static void run(int *p) { int *q = p; sink(q); }
int main(void) {
    int *x = malloc(sizeof *x);
    if (!x) return 1;
    run(x);
    return *x;
}

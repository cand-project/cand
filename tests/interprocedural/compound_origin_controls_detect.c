/*
 * Incident #64 controls (detection direction): destroying the origin of a
 * decided borrow must stay a FAIL detection.
 */
#include <stdlib.h>
struct S { int f; int arr[4]; };
static int *c_direct(int *p) { return p; }
static int *c_addr_member(struct S *p) { return &p->f; }
static int *c_arr_decay(struct S *p) { return p->arr; }
static int *c_addr_index(int *p, unsigned i) { return &p[i]; }
static int *c_arith(int *p, long n) { return p + n; }
static int *c_same_param(int *p, int c) { return c ? p : p; }
int main(void) {
    int *x = malloc(16 * sizeof(int));
    struct S *s = malloc(sizeof(struct S));
    if (!x || !s) return 1;
    int *a = c_direct(x);
    int *b = c_addr_member(s);
    int *d = c_arr_decay(s);
    int *e = c_addr_index(x, 2);
    int *g = c_arith(x, 3);
    int *h = c_same_param(x, 1);
    free(x);
    *a = 1; *b = 1; *d = 1; *e = 1; *g = 1; *h = 1;   /* all UAF */
    free(s);
    return 0;
}

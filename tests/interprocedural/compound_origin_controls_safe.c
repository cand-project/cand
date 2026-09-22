/*
 * Incident #64 controls: every accepted origin form must stay DECIDED --
 * direct p, &p->f, array-member decay, nested member chains, &p[i],
 * p + n, c ? p : p, c ? p : NULL, (side effect, p), and the incident #62
 * form (p = r; return r). If any of these over-collapses to Unknown, this
 * file gains obligations and stops passing.
 */
#include <stdlib.h>
struct S { int f; int arr[4]; struct { int arr2[4]; } s; };
static int *c_direct(int *p) { return p; }
static int *c_addr_member(struct S *p) { return &p->f; }
static int *c_arr_decay(struct S *p) { return p->arr; }
static int *c_nested(struct S *p) { return p->s.arr2; }
static int *c_addr_index(int *p, unsigned i) { return &p[i]; }
static int *c_arith(int *p, long n) { return p + n; }
static int *c_same_param(int *p, int c) { return c ? p : p; }
static int *c_with_null(int *p, int c) { return c ? p : NULL; }
static int *c_comma_ok(int *p) { return (p[0], p); }
static int *c_reassign_ok(int *p, int *r) { p = r; return r; }
int main(void) {
    int *x = malloc(16 * sizeof(int));
    struct S *s = malloc(sizeof(struct S));
    if (!x || !s) return 1;
    *c_direct(x) = 1;
    *c_addr_member(s) = 1;
    *c_arr_decay(s) = 1;
    c_arr_decay(s)[1] = 1;
    *c_nested(s) = 1;
    *c_addr_index(x, 2) = 1;
    *c_arith(x, 3) = 1;
    *c_same_param(x, 1) = 1;
    if (c_with_null(x, 1)) *c_with_null(x, 1) = 1;
    *c_comma_ok(x) = 1;
    int *r2 = malloc(sizeof(int));
    if (!r2) return 1;
    *c_reassign_ok(x, r2) = 1;
    free(x); free(s); free(r2);
    return 0;
}

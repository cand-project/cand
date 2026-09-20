#include <stdlib.h>

#define CAND_A(value) __attribute__((annotate(value)))
#define CAND_BORROW CAND_A("cand:borrow_shared")
#define CAND_TAKES CAND_A("cand:takes")
#define CAND_DESTROYS CAND_A("cand:destroys")
#define CAND_MOVE(value) (value)

#if defined(SIB_ARRAY)
static int array_read(int p[1] CAND_BORROW) { return p[0]; }
int main(void) { int *p = malloc(sizeof *p); *p = 1; int r = array_read(p); free(p); return r; }
#elif defined(SIB_TYPEDEF)
typedef int *IntPointer;
static int typedef_owner(IntPointer p CAND_TAKES) { free(p); return *p; }
int main(void) { return typedef_owner(CAND_MOVE(malloc(sizeof(int)))); }
#elif defined(SIB_CONST)
static int const_owner(int *const p CAND_TAKES) { free(p); return *p; }
int main(void) { return const_owner(CAND_MOVE(malloc(sizeof(int)))); }
#elif defined(SIB_MULTI)
static void destroy_two(int *a CAND_TAKES, int *b CAND_TAKES) { free(a); free(b); }
int main(void)
{
    int *a = malloc(sizeof *a);
    int *b = malloc(sizeof *b);
    destroy_two(CAND_MOVE(a), CAND_MOVE(b));
    return 0;
}
#elif defined(SIB_ALIAS_PARAMS)
static int alias_parameters(int *a CAND_TAKES, int *b CAND_TAKES)
{
    b = a;
    free(a);
    return *b;
}
int main(void)
{
    return alias_parameters(CAND_MOVE(malloc(sizeof(int))), CAND_MOVE(malloc(sizeof(int))));
}
#elif defined(SIB_NULL)
static int nullable_owner(int *p CAND_TAKES) { return p ? *p : 0; }
int main(void) { return nullable_owner(NULL); }
#elif defined(SIB_NULL_LIVE)
static int nullable_owner(int *p CAND_TAKES) { return p ? *p : 0; }
int main(void) { int *p = malloc(sizeof *p); *p = 1; return nullable_owner(CAND_MOVE(p)); }
#elif defined(SIB_OWNED_RETURN)
#define CAND_RETURNS_OWN CAND_A("cand:returns_own")
static CAND_RETURNS_OWN int *return_owner(int *p CAND_TAKES) { return p; }
int main(void)
{
    int *p = malloc(sizeof *p);
    int *q = return_owner(CAND_MOVE(p));
    free(q);
    return 0;
}
#elif defined(SIB_SAME)
static void destroy_same(int *a CAND_DESTROYS, int *b CAND_DESTROYS) { free(a); free(b); }
int main(void) { int *p = malloc(sizeof *p); destroy_same(p, p); return 0; }
#else
#error "select one SIB_* fixture"
#endif

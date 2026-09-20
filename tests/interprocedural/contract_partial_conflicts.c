#include <stdlib.h>

static void explicit_no_effect(int *p)
{
    if (p) (void)*p;
}

static int *owned_but_borrowed(int *p)
{
    return p;
}

static void destroys_but_borrows(int *p)
{
    if (p) (void)*p;
}

extern int *unknown_body_source(void);
static int *unknown_body(void)
{
    return unknown_body_source();
}

static int *conditional_body(int *p, int choose)
{
    return choose ? malloc(sizeof(int)) : p;
}

#define CAND_RETURNS_OWN __attribute__((annotate("cand:returns_own")))
static CAND_RETURNS_OWN int *annotation_disagrees(int *p)
{
    return p;
}

int main(void)
{
    int *p = malloc(sizeof(int));
    explicit_no_effect(p);
    int *a = owned_but_borrowed(p);
    (void)a;
    destroys_but_borrows(p);
    int *b = unknown_body();
    (void)b;
    int *c = conditional_body(p, 1);
    (void)c;
    int *d = annotation_disagrees(p);
    (void)d;
    free(p);
    return 0;
}

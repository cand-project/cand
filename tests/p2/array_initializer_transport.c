#include <stdlib.h>
#include "p2_compat.h"

typedef struct Node { int value; } Node;
typedef struct Registry { Node active; } Registry;
struct Ref { Node *node; };

CAND_RETURNS_OWN Registry *registry_new(void)
{
    Registry *registry = malloc(sizeof(*registry));
    if (registry != NULL) registry->active.value = 3;
    return registry;
}

CAND_RETURNS_BORROW_FROM(0) Node *registry_active(Registry *registry)
{
    return &registry->active;
}

int main(void)
{
    Registry *registry CAND_OWN = registry_new();
    Node *node CAND_BORROW = registry_active(registry);
    struct Ref refs[2] = {{0}, {node}};

    free(registry);
    return refs[1].node->value;
}

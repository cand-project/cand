#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "cand/cand.h"

typedef struct Item {
    int value;
} Item;

static Item *make_item(void) CAND_RETURNS_OWN;
static Item *make_item(void) {
    Item *item = malloc(sizeof *item);
    if (item != NULL) item->value = 41;
    return item;
}

static int consume(Item *item) CAND_TAKES;
static int consume(Item *item) {
    int value = item->value;
    free(item);
    return value;
}

static int borrow_value(const Item *item) CAND_BORROW;
static int borrow_value(const Item *item) {
    return item->value;
}

static int mutate_value(Item *item) CAND_BORROW_MUT;
static int mutate_value(Item *item) {
    item->value++;
    return item->value;
}

int main(void) {
    _Static_assert(sizeof(Item) == sizeof(int), "layout changed");
    _Static_assert(_Alignof(Item) == _Alignof(int), "alignment changed");
    Item *owned = make_item();
    if (owned == NULL || borrow_value(owned) != 41 || mutate_value(owned) != 42) return 1;
    return consume(CAND_MOVE(owned)) == 42 ? 0 : 1;
}

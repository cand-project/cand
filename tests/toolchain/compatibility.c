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

static Item *borrow_item(Item *item) CAND_RETURNS_BORROW_FROM(0);
static Item *borrow_item(Item *item) {
    return item;
}

int main(void) {
    _Static_assert(sizeof(Item) == sizeof(int), "layout changed");
    _Static_assert(_Alignof(Item) == _Alignof(int), "alignment changed");
    Item *owned CAND_OWN = make_item();
    Item *borrowed = owned == NULL ? NULL : borrow_item(owned);
    if (owned == NULL || borrowed != owned || borrow_value(borrowed) != 41 ||
        mutate_value(owned) != 42) return 1;
    return consume(CAND_MOVE(owned)) == 42 ? 0 : 1;
}

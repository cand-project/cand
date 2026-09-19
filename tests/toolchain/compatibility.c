#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "cand/cand.h"

typedef struct Item {
    int value;
} Item;

Item *make_item(void) CAND_RETURNS_OWN;
Item *make_item(void) {
    Item *item = malloc(sizeof *item);
    if (item != NULL) item->value = 41;
    return item;
}

int consume(Item *item) CAND_TAKES;
int consume(Item *item) {
    int value = item->value;
    free(item);
    return value;
}

int borrow_value(const Item *item) CAND_BORROW;
int borrow_value(const Item *item) {
    return item->value;
}

int mutate_value(Item *item) CAND_BORROW_MUT;
int mutate_value(Item *item) {
    item->value++;
    return item->value;
}

Item *borrow_item(Item *item) CAND_RETURNS_BORROW_FROM(0);
Item *borrow_item(Item *item) {
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

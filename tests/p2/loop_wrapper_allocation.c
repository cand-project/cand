#include <stdlib.h>
#include "p2_compat.h"

typedef struct Cell { int value; } Cell;
typedef Cell View;

CAND_RETURNS_OWN Cell *make_cell(void)
{
    return malloc(sizeof(Cell));
}

CAND_RETURNS_BORROW_FROM(0) View *view_of(Cell *cell)
{
    return cell;
}

int main(void)
{
    Cell *owner CAND_OWN = make_cell();
    View *view CAND_BORROW = view_of(owner);

    for (int i = 0; i < 2; ++i) {
        Cell *extra CAND_OWN = make_cell();
        free(extra);
    }

    (void)view->value;
    free(owner);
    return 0;
}

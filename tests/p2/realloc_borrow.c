#include <stdlib.h>
#include "p2_compat.h"

int main(void)
{
    char *buffer CAND_OWN = malloc(8);
    char *view CAND_BORROW = buffer;
    buffer = realloc(buffer, 128);
    return view[0];
}

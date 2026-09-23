#include <stdlib.h>

#define CAND_BORROW __attribute__((annotate("cand:borrow")))

/* Reviewed borrow-parameter boundary: the external callee only reads
 * the argument; the caller keeps ownership. */
extern void reviewed_read(int *data CAND_BORROW);

int main(void)
{
    int *data = malloc(sizeof *data);
    if (data == NULL) return 0;
    reviewed_read(data);
    free(data);
    return 0;
}

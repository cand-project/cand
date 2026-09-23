#include <stdlib.h>

#define CAND_BORROW __attribute__((annotate("cand:borrow")))

/* Reviewed callback-taking boundary. The function-pointer parameter has
 * no ownership effect; the data parameter borrows. The verdict must
 * match the equivalent reviewed contract shape (H1 parity). */
extern void reviewed_register(void (*callback)(void *), int *data CAND_BORROW);

static void on_event(void *ctx)
{
    (void)ctx;
}

int main(void)
{
    int storage = 7;
    reviewed_register(on_event, &storage);
    return storage;
}

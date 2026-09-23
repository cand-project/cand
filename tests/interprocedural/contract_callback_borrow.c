#include <stdlib.h>

/* Contract twin of annotation_review_callback_borrow.c: the identical
 * external shape resolved through a reviewed contract instead of a
 * declaration annotation. The two modes must agree verdict-for-verdict
 * (H1 parity). */
extern void reviewed_register(void (*callback)(void *), int *data);

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

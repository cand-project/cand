#include <stddef.h>
#include "fixture-metadata.h"

void fixture_consume(void *value CAND_TAKES);

int fixture_case(void) {
    void *value = (void *)1;
    fixture_consume(value);
    return 0;
}

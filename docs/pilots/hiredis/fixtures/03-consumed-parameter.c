#include <stdlib.h>
#include "fixture-metadata.h"

void fixture_consume(void *value CAND_TAKES);

int fixture_case(void) {
    void *value = malloc(8);
    fixture_consume(CAND_MOVE(value));
    return 0;
}

#include <stdlib.h>
#include "fixture-metadata.h"

void *fixture_matching(void) CAND_RETURNS_OWN {
    return malloc(8);
}

int fixture_case(void) {
    void *value = fixture_matching();
    free(value);
    return 0;
}

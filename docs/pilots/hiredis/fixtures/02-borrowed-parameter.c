#include "fixture-metadata.h"

int fixture_borrow(const char *value CAND_BORROW) {
    return value != NULL ? value[0] : 0;
}

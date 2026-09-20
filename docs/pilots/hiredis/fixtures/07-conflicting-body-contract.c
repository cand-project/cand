#include "fixture-metadata.h"

void *fixture_conflicting(const char *borrowed) CAND_RETURNS_OWN {
    return (void *)borrowed;
}

int fixture_case(const char *value) {
    return fixture_conflicting(value) != NULL;
}

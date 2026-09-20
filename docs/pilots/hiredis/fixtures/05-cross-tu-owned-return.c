#include "fixture-metadata.h"

void *fixture_external_owned(void) CAND_RETURNS_OWN;
void fixture_external_destroy(void *value CAND_DESTROYS);

int fixture_case(void) {
    void *value = fixture_external_owned();
    fixture_external_destroy(value);
    return 0;
}

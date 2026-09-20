#include <stddef.h>
#include "fixture-metadata.h"

void *fixture_owned(void) CAND_RETURNS_OWN;
void fixture_destroy(void *value CAND_DESTROYS);

int fixture_case(void) {
    void *value = fixture_owned();
    fixture_destroy(value);
    return value != NULL;
}

#include <stddef.h>
#include "fixture-metadata.h"

void *fixture_realloc(void *value, size_t size);

int fixture_case(void) {
    void *value = (void *)1;
    value = fixture_realloc(value, 16);
    return value != NULL;
}

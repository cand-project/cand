#include <stddef.h>
#include "fixture-metadata.h"

int fixture_out(void **value);

int fixture_case(void) {
    void *value = NULL;
    return fixture_out(&value);
}

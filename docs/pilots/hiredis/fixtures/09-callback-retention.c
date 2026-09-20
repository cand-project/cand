#include "fixture-metadata.h"

typedef void (*fixture_callback)(void *data);
static fixture_callback saved_callback;
static void *saved_data;

void fixture_register(fixture_callback callback, void *data) {
    saved_callback = callback;
    saved_data = data;
}

int fixture_case(void *data) {
    fixture_register((fixture_callback)0, data);
    return saved_data != NULL;
}

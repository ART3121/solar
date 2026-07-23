#ifndef SOLAR_CONFIG_EDIT_H
#define SOLAR_CONFIG_EDIT_H

#include "solar/result.h"

#include <stdbool.h>

typedef struct {
    /* All strings are borrowed for the duration of the call. NULL means keep. */
    const char *project_name;
    const char *synthesis_top;
    const char *default_test;
} SolarConfigEditRequest;

/*
 * Updates selected format-2 keys only after a candidate manifest parses and
 * validates. The original solar.toml remains untouched on every failure.
 */
SolarResult solar_config_edit_project(
    const char *start_path,
    const SolarConfigEditRequest *request,
    bool *changed_out
);

#endif

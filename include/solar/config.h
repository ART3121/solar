#ifndef SOLAR_CONFIG_H
#define SOLAR_CONFIG_H

#include "solar/result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char **items;
    size_t count;
} SolarStringList;

typedef struct {
    char *name;
    char *top;
    char *language;
    char *default_test;
    char *default_profile;
} SolarProjectSettings;

typedef struct {
    SolarStringList rtl;
    SolarStringList testbench;
    SolarStringList include_dirs;
    SolarStringList defines;
} SolarSourceSettings;

typedef struct {
    char *backend;
    char *top;
    char *waveform;
} SolarSimulationSettings;

typedef struct {
    char *backend;
    char *top;
} SolarSynthesisSettings;

typedef struct {
    char *backend;
    char *source;
    char *processor;
    uint64_t frequency_mhz;
    uint64_t simulation_clocks;
    SolarStringList include_dirs;
} SolarCompilerSettings;

typedef struct {
    char *root;
    char *diagnostics;
} SolarYancSettings;

typedef struct {
    char *name;
    SolarStringList include_dirs;
    SolarStringList defines;
} SolarProfile;

typedef enum {
    SOLAR_TEST_VERILOG = 0,
    SOLAR_TEST_GENERATED
} SolarTestKind;

typedef struct {
    char *name;
    char *top;
    SolarStringList sources;
    SolarStringList include_dirs;
    SolarStringList defines;
    char *waveform;
    char *driver;
    char *cocotb;
    SolarTestKind kind;
} SolarTest;

typedef struct {
    SolarStringList include_dirs;
    SolarStringList defines;
} SolarEffectiveConfig;

typedef struct {
    unsigned int manifest_format;
    SolarProjectSettings project;
    SolarSourceSettings sources;
    SolarSimulationSettings simulation;
    SolarSynthesisSettings synthesis;
    SolarCompilerSettings compiler;
    SolarYancSettings yanc;
    SolarProfile *profiles;
    size_t profile_count;
    SolarTest *tests;
    size_t test_count;
} SolarConfig;

void solar_string_list_init(SolarStringList *list);
void solar_string_list_free(SolarStringList *list);
SolarResult solar_string_list_append_copy(
    SolarStringList *list,
    const char *value
);

void solar_config_init(SolarConfig *config);
void solar_config_free(SolarConfig *config);

/*
 * config must be initialized and empty. On success it owns all parsed strings
 * until solar_config_free().
 */
SolarResult solar_config_parse_file(const char *manifest_path, SolarConfig *config);
SolarResult solar_config_validate(
    const SolarConfig *config,
    const char *manifest_path
);

/* Returned profile/test pointers are borrowed from config. */
const SolarProfile *solar_config_find_profile(
    const SolarConfig *config,
    const char *name
);
const SolarTest *solar_config_find_test(
    const SolarConfig *config,
    const char *name
);

SolarResult solar_config_select_profile(
    const SolarConfig *config,
    const char *requested_name,
    const SolarProfile **profile_out
);
SolarResult solar_config_select_test(
    const SolarConfig *config,
    const char *requested_name,
    const SolarTest **test_out
);

void solar_effective_config_init(SolarEffectiveConfig *effective);
void solar_effective_config_free(SolarEffectiveConfig *effective);

/*
 * Produces owned, de-duplicated global -> profile -> test lists. Passing a null
 * test produces the synthesis configuration (global -> profile only).
 */
SolarResult solar_config_build_effective(
    const SolarConfig *config,
    const SolarProfile *profile,
    const SolarTest *test,
    SolarEffectiveConfig *effective
);

#endif

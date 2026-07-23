#define _POSIX_C_SOURCE 200809L

#include "solar/config.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int report_failure(const char *test_name, const char *message)
{
    (void)fprintf(stderr, "%s: %s\n", test_name, message);
    return 1;
}

static bool string_list_equals(
    const SolarStringList *list,
    const char *const *expected,
    size_t expected_count
)
{
    size_t index;

    if (list->count != expected_count) {
        return false;
    }
    for (index = 0U; index < expected_count; index++) {
        if (strcmp(list->items[index], expected[index]) != 0) {
            return false;
        }
    }
    return true;
}

static int append_values(
    const char *test_name,
    SolarStringList *list,
    const char *const *values,
    size_t count
)
{
    size_t index;

    for (index = 0U; index < count; index++) {
        SolarResult result = solar_string_list_append_copy(list, values[index]);

        if (result.status != SOLAR_STATUS_OK) {
            return report_failure(test_name, result.diagnostic.message);
        }
    }
    return 0;
}

static int prepare_config(const char *test_name, SolarConfig *config)
{
    const char *const global_includes[] = {
        "include/common", "include/global"
    };
    const char *const global_defines[] = {
        "COMMON", "DATA_WIDTH=8"
    };
    const char *const profile_includes[] = {
        "include/common", "include/debug"
    };
    const char *const profile_defines[] = {
        "COMMON", "DEBUG"
    };
    const char *const test_includes[] = {
        "include/debug", "include/test"
    };
    const char *const test_defines[] = {
        "DATA_WIDTH=8", "TEST_BASIC"
    };
    int failed = 0;

    solar_config_init(config);
    config->profiles = calloc(1U, sizeof(*config->profiles));
    config->tests = calloc(1U, sizeof(*config->tests));
    if (config->profiles == NULL || config->tests == NULL) {
        return report_failure(test_name, "could not allocate effective-config fixture");
    }
    config->profile_count = 1U;
    config->test_count = 1U;
    config->profiles[0].name = strdup("debug");
    config->tests[0].name = strdup("basic");
    if (config->profiles[0].name == NULL || config->tests[0].name == NULL) {
        return report_failure(test_name, "could not allocate fixture names");
    }

    failed += append_values(
        test_name,
        &config->sources.include_dirs,
        global_includes,
        sizeof(global_includes) / sizeof(global_includes[0])
    );
    failed += append_values(
        test_name,
        &config->sources.defines,
        global_defines,
        sizeof(global_defines) / sizeof(global_defines[0])
    );
    failed += append_values(
        test_name,
        &config->profiles[0].include_dirs,
        profile_includes,
        sizeof(profile_includes) / sizeof(profile_includes[0])
    );
    failed += append_values(
        test_name,
        &config->profiles[0].defines,
        profile_defines,
        sizeof(profile_defines) / sizeof(profile_defines[0])
    );
    failed += append_values(
        test_name,
        &config->tests[0].include_dirs,
        test_includes,
        sizeof(test_includes) / sizeof(test_includes[0])
    );
    failed += append_values(
        test_name,
        &config->tests[0].defines,
        test_defines,
        sizeof(test_defines) / sizeof(test_defines[0])
    );
    return failed;
}

static int expect_effective(
    const char *test_name,
    const SolarConfig *config,
    const SolarProfile *profile,
    const SolarTest *test,
    const char *const *expected_includes,
    size_t include_count,
    const char *const *expected_defines,
    size_t define_count
)
{
    SolarEffectiveConfig effective;
    SolarResult result;
    int failed = 0;

    solar_effective_config_init(&effective);
    result = solar_config_build_effective(config, profile, test, &effective);
    if (result.status != SOLAR_STATUS_OK) {
        failed = report_failure(test_name, result.diagnostic.message);
        goto cleanup;
    }
    if (!string_list_equals(
            &effective.include_dirs, expected_includes, include_count
        ) ||
        !string_list_equals(
            &effective.defines, expected_defines, define_count
        )) {
        failed = report_failure(
            test_name,
            "effective lists do not preserve precedence, order, and uniqueness"
        );
        goto cleanup;
    }

    if (effective.include_dirs.count > 0U &&
        effective.include_dirs.items[0] == config->sources.include_dirs.items[0]) {
        failed = report_failure(test_name, "effective include strings are borrowed");
    }
    if (effective.defines.count > 0U &&
        effective.defines.items[0] == config->sources.defines.items[0]) {
        failed = report_failure(test_name, "effective define strings are borrowed");
    }

cleanup:
    solar_effective_config_free(&effective);
    return failed;
}

static int verify_original_config(const char *test_name, const SolarConfig *config)
{
    const char *const global_includes[] = {
        "include/common", "include/global"
    };
    const char *const global_defines[] = {
        "COMMON", "DATA_WIDTH=8"
    };
    const char *const profile_includes[] = {
        "include/common", "include/debug"
    };
    const char *const profile_defines[] = {
        "COMMON", "DEBUG"
    };
    const char *const test_includes[] = {
        "include/debug", "include/test"
    };
    const char *const test_defines[] = {
        "DATA_WIDTH=8", "TEST_BASIC"
    };

    if (!string_list_equals(
            &config->sources.include_dirs,
            global_includes,
            sizeof(global_includes) / sizeof(global_includes[0])
        ) ||
        !string_list_equals(
            &config->sources.defines,
            global_defines,
            sizeof(global_defines) / sizeof(global_defines[0])
        ) ||
        !string_list_equals(
            &config->profiles[0].include_dirs,
            profile_includes,
            sizeof(profile_includes) / sizeof(profile_includes[0])
        ) ||
        !string_list_equals(
            &config->profiles[0].defines,
            profile_defines,
            sizeof(profile_defines) / sizeof(profile_defines[0])
        ) ||
        !string_list_equals(
            &config->tests[0].include_dirs,
            test_includes,
            sizeof(test_includes) / sizeof(test_includes[0])
        ) ||
        !string_list_equals(
            &config->tests[0].defines,
            test_defines,
            sizeof(test_defines) / sizeof(test_defines[0])
        )) {
        return report_failure(test_name, "effective merge mutated source configuration");
    }
    return 0;
}

static int test_effective_config_precedence(void)
{
    const char *test_name = "effective configuration precedence";
    const char *const global_includes[] = {
        "include/common", "include/global"
    };
    const char *const global_defines[] = {
        "COMMON", "DATA_WIDTH=8"
    };
    const char *const profile_includes[] = {
        "include/common", "include/global", "include/debug"
    };
    const char *const profile_defines[] = {
        "COMMON", "DATA_WIDTH=8", "DEBUG"
    };
    const char *const test_includes[] = {
        "include/common", "include/global", "include/debug", "include/test"
    };
    const char *const test_defines[] = {
        "COMMON", "DATA_WIDTH=8", "TEST_BASIC"
    };
    const char *const all_defines[] = {
        "COMMON", "DATA_WIDTH=8", "DEBUG", "TEST_BASIC"
    };
    SolarConfig config;
    int failures;

    failures = prepare_config(test_name, &config);
    if (failures != 0) {
        solar_config_free(&config);
        return failures;
    }

    failures += expect_effective(
        "effective globals",
        &config,
        NULL,
        NULL,
        global_includes,
        sizeof(global_includes) / sizeof(global_includes[0]),
        global_defines,
        sizeof(global_defines) / sizeof(global_defines[0])
    );
    failures += expect_effective(
        "effective globals and profile",
        &config,
        &config.profiles[0],
        NULL,
        profile_includes,
        sizeof(profile_includes) / sizeof(profile_includes[0]),
        profile_defines,
        sizeof(profile_defines) / sizeof(profile_defines[0])
    );
    failures += expect_effective(
        "effective globals and test",
        &config,
        NULL,
        &config.tests[0],
        test_includes,
        sizeof(test_includes) / sizeof(test_includes[0]),
        test_defines,
        sizeof(test_defines) / sizeof(test_defines[0])
    );
    failures += expect_effective(
        "effective globals, profile, and test",
        &config,
        &config.profiles[0],
        &config.tests[0],
        test_includes,
        sizeof(test_includes) / sizeof(test_includes[0]),
        all_defines,
        sizeof(all_defines) / sizeof(all_defines[0])
    );
    failures += verify_original_config(test_name, &config);

    solar_config_free(&config);
    return failures;
}

int main(void)
{
    return test_effective_config_precedence() == 0 ? 0 : 1;
}

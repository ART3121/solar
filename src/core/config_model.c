#include "solar/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void solar_string_list_init(SolarStringList *list)
{
    if (list == NULL) return;
    list->items = NULL;
    list->count = 0U;
}

void solar_string_list_free(SolarStringList *list)
{
    size_t index;

    if (list == NULL) return;
    for (index = 0U; index < list->count; index++) {
        free(list->items[index]);
    }
    free(list->items);
    solar_string_list_init(list);
}

SolarResult solar_string_list_append_copy(
    SolarStringList *list,
    const char *value
)
{
    char **items;
    char *copy;

    if (list == NULL || value == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot append a null configuration value",
            "provide a string list and value"
        );
    }
    copy = strdup(value);
    if (copy == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not copy a configuration value",
            "free memory and try again"
        );
    }
    items = realloc(list->items, (list->count + 1U) * sizeof(*items));
    if (items == NULL) {
        free(copy);
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not grow a configuration list",
            "free memory and try again"
        );
    }
    list->items = items;
    list->items[list->count++] = copy;
    return solar_result_ok();
}

const SolarProfile *solar_config_find_profile(
    const SolarConfig *config,
    const char *name
)
{
    size_t index;

    if (config == NULL || name == NULL) return NULL;
    for (index = 0U; index < config->profile_count; index++) {
        if (config->profiles[index].name != NULL &&
            strcmp(config->profiles[index].name, name) == 0) {
            return &config->profiles[index];
        }
    }
    return NULL;
}

const SolarTest *solar_config_find_test(
    const SolarConfig *config,
    const char *name
)
{
    size_t index;

    if (config == NULL || name == NULL) return NULL;
    for (index = 0U; index < config->test_count; index++) {
        if (config->tests[index].name != NULL &&
            strcmp(config->tests[index].name, name) == 0) {
            return &config->tests[index];
        }
    }
    return NULL;
}

static SolarResult missing_named_value(
    const char *kind,
    const char *name,
    const char *available_hint
)
{
    SolarResult result;

    result.status = SOLAR_STATUS_CONFIG_ERROR;
    solar_diagnostic_format(
        &result.diagnostic,
        SOLAR_DIAGNOSTIC_ERROR,
        available_hint,
        "%s \"%s\" does not exist",
        kind,
        name
    );
    return result;
}

static void append_available_names(
    char *hint,
    size_t capacity,
    const SolarConfig *config,
    bool profiles
)
{
    size_t count = profiles ? config->profile_count : config->test_count;
    size_t index;

    (void)snprintf(hint, capacity, "available %s: ", profiles ? "profiles" : "tests");
    for (index = 0U; index < count; index++) {
        const char *name = profiles
            ? config->profiles[index].name
            : config->tests[index].name;
        size_t used = strlen(hint);

        if (used >= capacity - 1U) break;
        (void)snprintf(
            hint + used,
            capacity - used,
            "%s%s",
            index == 0U ? "" : ", ",
            name == NULL ? "?" : name
        );
    }
}

SolarResult solar_config_select_profile(
    const SolarConfig *config,
    const char *requested_name,
    const SolarProfile **profile_out
)
{
    const char *selected_name = requested_name;
    char hint[SOLAR_DIAGNOSTIC_HINT_SIZE];

    if (profile_out != NULL) *profile_out = NULL;
    if (config == NULL || profile_out == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot select a profile without configuration and output storage",
            "load a project before selecting a profile"
        );
    }
    if (selected_name == NULL || selected_name[0] == '\0') {
        selected_name = config->project.default_profile;
    }
    if (selected_name == NULL || selected_name[0] == '\0') {
        return solar_result_ok();
    }
    *profile_out = solar_config_find_profile(config, selected_name);
    if (*profile_out != NULL) return solar_result_ok();
    append_available_names(hint, sizeof(hint), config, true);
    return missing_named_value("profile", selected_name, hint);
}

SolarResult solar_config_select_test(
    const SolarConfig *config,
    const char *requested_name,
    const SolarTest **test_out
)
{
    const char *selected_name = requested_name;
    char hint[SOLAR_DIAGNOSTIC_HINT_SIZE];

    if (test_out != NULL) *test_out = NULL;
    if (config == NULL || test_out == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot select a test without configuration and output storage",
            "load a project before selecting a test"
        );
    }
    if (selected_name == NULL || selected_name[0] == '\0') {
        selected_name = config->project.default_test;
    }
    if ((selected_name == NULL || selected_name[0] == '\0') &&
        config->test_count == 1U) {
        *test_out = &config->tests[0];
        return solar_result_ok();
    }
    if (selected_name == NULL || selected_name[0] == '\0') {
        if (config->test_count == 0U) {
            return solar_result_error(
                SOLAR_STATUS_CONFIG_ERROR,
                "project defines no tests",
                "add a [[test]] table or configure a compiler-generated test"
            );
        }
        return solar_result_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "project has multiple tests but no default test",
            "set [project].default_test or pass a test name"
        );
    }
    *test_out = solar_config_find_test(config, selected_name);
    if (*test_out != NULL) return solar_result_ok();
    append_available_names(hint, sizeof(hint), config, false);
    return missing_named_value("test", selected_name, hint);
}

void solar_effective_config_init(SolarEffectiveConfig *effective)
{
    if (effective == NULL) return;
    solar_string_list_init(&effective->include_dirs);
    solar_string_list_init(&effective->defines);
}

void solar_effective_config_free(SolarEffectiveConfig *effective)
{
    if (effective == NULL) return;
    solar_string_list_free(&effective->include_dirs);
    solar_string_list_free(&effective->defines);
}

static bool list_contains(const SolarStringList *list, const char *value)
{
    size_t index;

    for (index = 0U; index < list->count; index++) {
        if (strcmp(list->items[index], value) == 0) return true;
    }
    return false;
}

static SolarResult append_unique_list(
    SolarStringList *destination,
    const SolarStringList *source
)
{
    size_t index;

    for (index = 0U; index < source->count; index++) {
        SolarResult result;

        if (list_contains(destination, source->items[index])) continue;
        result = solar_string_list_append_copy(destination, source->items[index]);
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    return solar_result_ok();
}

SolarResult solar_config_build_effective(
    const SolarConfig *config,
    const SolarProfile *profile,
    const SolarTest *test,
    SolarEffectiveConfig *effective
)
{
    SolarResult result;

    if (effective != NULL) solar_effective_config_init(effective);
    if (config == NULL || effective == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot build effective configuration without input and output storage",
            "load a project before building effective configuration"
        );
    }
    result = append_unique_list(&effective->include_dirs, &config->sources.include_dirs);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = append_unique_list(&effective->defines, &config->sources.defines);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (profile != NULL) {
        result = append_unique_list(&effective->include_dirs, &profile->include_dirs);
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        result = append_unique_list(&effective->defines, &profile->defines);
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
    }
    if (test != NULL) {
        result = append_unique_list(&effective->include_dirs, &test->include_dirs);
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        result = append_unique_list(&effective->defines, &test->defines);
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
    }
    return solar_result_ok();

cleanup:
    solar_effective_config_free(effective);
    return result;
}

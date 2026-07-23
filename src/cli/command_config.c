#include "commands.h"

#include "solar/config_edit.h"

#include <stdio.h>
#include <string.h>

static SolarResult missing_value(const char *option)
{
    SolarResult result = solar_result_error(
        SOLAR_STATUS_INVALID_ARGUMENT,
        "missing solar config value",
        "provide a non-empty value after each configuration option"
    );

    (void)snprintf(
        result.diagnostic.message,
        sizeof(result.diagnostic.message),
        "missing value for %s",
        option
    );
    return result;
}

static SolarResult set_option(
    const char *option,
    const char *value,
    SolarConfigEditRequest *request
)
{
    const char **destination = NULL;

    if (strcmp(option, "--name") == 0) {
        destination = &request->project_name;
    } else if (strcmp(option, "--top") == 0) {
        destination = &request->synthesis_top;
    } else if (strcmp(option, "--test") == 0) {
        destination = &request->default_test;
    } else {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "unknown solar config option",
            "use --name, --top, or --test"
        );
    }
    if (*destination != NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "configuration option was provided more than once",
            "provide each option at most once"
        );
    }
    if (value == NULL || value[0] == '\0') return missing_value(option);
    *destination = value;
    return solar_result_ok();
}

SolarResult solar_cli_command_config(
    const char *start_path,
    int argument_count,
    char *const arguments[]
)
{
    SolarConfigEditRequest request = {0};
    bool changed = false;
    SolarResult result;
    int index;

    if (argument_count < 3 || strcmp(arguments[0], "set") != 0) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "invalid solar config arguments",
            "use solar config set [--name <name>] [--top <module>] [--test <test>]"
        );
    }
    for (index = 1; index < argument_count; index += 2) {
        if (index + 1 >= argument_count) return missing_value(arguments[index]);
        result = set_option(
            arguments[index], arguments[index + 1], &request
        );
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    result = solar_config_edit_project(start_path, &request, &changed);
    if (result.status != SOLAR_STATUS_OK) return result;
    (void)printf("Solar config: %s\n", changed ? "updated" : "unchanged");
    if (request.project_name != NULL) {
        (void)printf("  name:         %s\n", request.project_name);
    }
    if (request.synthesis_top != NULL) {
        (void)printf("  top:          %s\n", request.synthesis_top);
    }
    if (request.default_test != NULL) {
        (void)printf("  default test: %s\n", request.default_test);
    }
    return solar_result_ok();
}

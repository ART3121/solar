#include "commands.h"

#include "solar/project.h"

#include <stdio.h>
#include <string.h>

SolarResult solar_cli_command_init(
    const char *directory,
    int argument_count,
    char *const arguments[]
)
{
    const char *template_name = "verilog";
    const char *internal_template = "verilog";
    SolarResult result;

    if (argument_count != 0) {
        if (argument_count != 2 || strcmp(arguments[0], "--template") != 0) {
            return solar_result_error(
                SOLAR_STATUS_INVALID_ARGUMENT,
                "invalid solar init arguments",
                "use solar init [--template verilog|sapho]"
            );
        }
        template_name = arguments[1];
        if (strcmp(template_name, "verilog") == 0) {
            internal_template = "verilog";
        } else if (strcmp(template_name, "sapho") == 0) {
            internal_template = "yanc-cmm";
        } else {
            return solar_result_error(
                SOLAR_STATUS_INVALID_ARGUMENT,
                "unknown project template",
                "use solar init [--template verilog|sapho]"
            );
        }
    }
    result = solar_project_initialize_template(directory, internal_template);

    if (result.status == SOLAR_STATUS_OK) {
        (void)printf(
            "Initialized Solar project in the current directory (template: %s).\n",
            template_name
        );
    }
    return result;
}

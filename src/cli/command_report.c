#include "commands.h"

#include "solar/build.h"

#include <stdio.h>
#include <stdlib.h>

SolarResult solar_cli_command_report(const char *start_path)
{
    char *text = NULL;
    SolarResult result = solar_build_report_read(start_path, &text);

    if (result.status == SOLAR_STATUS_OK) {
        (void)printf("%s", text);
    }
    free(text);
    return result;
}

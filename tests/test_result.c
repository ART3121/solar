#include "solar/result.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    SolarResult success = solar_result_ok();
    SolarResult failure = solar_result_error(
        SOLAR_STATUS_CONFIG_ERROR,
        "solar.toml: [project].top is required",
        "add top under [project]"
    );

    if (success.status != SOLAR_STATUS_OK ||
        success.diagnostic.severity != SOLAR_DIAGNOSTIC_NONE) {
        (void)fprintf(stderr, "success result was not initialized\n");
        return 1;
    }

    if (failure.status != SOLAR_STATUS_CONFIG_ERROR ||
        strstr(failure.diagnostic.message, "project") == NULL ||
        failure.diagnostic.hint[0] == '\0') {
        (void)fprintf(stderr, "failure result lost its diagnostic\n");
        return 1;
    }

    return 0;
}

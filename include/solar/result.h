#ifndef SOLAR_RESULT_H
#define SOLAR_RESULT_H

#include "solar/diagnostics.h"

typedef enum {
    SOLAR_STATUS_OK = 0,
    SOLAR_STATUS_INVALID_ARGUMENT,
    SOLAR_STATUS_CONFIG_ERROR,
    SOLAR_STATUS_NOT_FOUND,
    SOLAR_STATUS_IO_ERROR,
    SOLAR_STATUS_TOOL_MISSING,
    SOLAR_STATUS_PROCESS_FAILED,
    SOLAR_STATUS_INTERNAL_ERROR
} SolarStatus;

typedef struct {
    SolarStatus status;
    SolarDiagnostic diagnostic;
} SolarResult;

SolarResult solar_result_ok(void);
SolarResult solar_result_error(
    SolarStatus status,
    const char *message,
    const char *hint
);
const char *solar_status_name(SolarStatus status);

#endif

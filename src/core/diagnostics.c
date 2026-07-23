#include "solar/result.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (capacity == 0U) {
        return;
    }

    if (source == NULL) {
        destination[0] = '\0';
        return;
    }

    (void)snprintf(destination, capacity, "%s", source);
}

void solar_diagnostic_clear(SolarDiagnostic *diagnostic)
{
    if (diagnostic == NULL) {
        return;
    }

    diagnostic->severity = SOLAR_DIAGNOSTIC_NONE;
    diagnostic->message[0] = '\0';
    diagnostic->hint[0] = '\0';
}

void solar_diagnostic_set(
    SolarDiagnostic *diagnostic,
    SolarDiagnosticSeverity severity,
    const char *message,
    const char *hint
)
{
    if (diagnostic == NULL) {
        return;
    }

    diagnostic->severity = severity;
    copy_text(diagnostic->message, sizeof(diagnostic->message), message);
    copy_text(diagnostic->hint, sizeof(diagnostic->hint), hint);
}

void solar_diagnostic_format(
    SolarDiagnostic *diagnostic,
    SolarDiagnosticSeverity severity,
    const char *hint,
    const char *format,
    ...
)
{
    va_list arguments;

    if (diagnostic == NULL || format == NULL) {
        return;
    }

    diagnostic->severity = severity;
    va_start(arguments, format);
    (void)vsnprintf(
        diagnostic->message,
        sizeof(diagnostic->message),
        format,
        arguments
    );
    va_end(arguments);
    copy_text(diagnostic->hint, sizeof(diagnostic->hint), hint);
}

SolarResult solar_result_ok(void)
{
    SolarResult result;

    result.status = SOLAR_STATUS_OK;
    solar_diagnostic_clear(&result.diagnostic);
    return result;
}

SolarResult solar_result_error(
    SolarStatus status,
    const char *message,
    const char *hint
)
{
    SolarResult result;

    result.status = status;
    solar_diagnostic_set(
        &result.diagnostic,
        SOLAR_DIAGNOSTIC_ERROR,
        message,
        hint
    );
    return result;
}

const char *solar_status_name(SolarStatus status)
{
    switch (status) {
    case SOLAR_STATUS_OK:
        return "ok";
    case SOLAR_STATUS_INVALID_ARGUMENT:
        return "invalid argument";
    case SOLAR_STATUS_CONFIG_ERROR:
        return "configuration error";
    case SOLAR_STATUS_NOT_FOUND:
        return "not found";
    case SOLAR_STATUS_IO_ERROR:
        return "I/O error";
    case SOLAR_STATUS_TOOL_MISSING:
        return "tool missing";
    case SOLAR_STATUS_PROCESS_FAILED:
        return "process failed";
    case SOLAR_STATUS_INTERNAL_ERROR:
        return "internal error";
    }

    return "unknown status";
}

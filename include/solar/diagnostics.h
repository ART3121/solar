#ifndef SOLAR_DIAGNOSTICS_H
#define SOLAR_DIAGNOSTICS_H

#include <stddef.h>

#define SOLAR_DIAGNOSTIC_MESSAGE_SIZE 512U
#define SOLAR_DIAGNOSTIC_HINT_SIZE 256U

typedef enum {
    SOLAR_DIAGNOSTIC_NONE = 0,
    SOLAR_DIAGNOSTIC_WARNING,
    SOLAR_DIAGNOSTIC_ERROR
} SolarDiagnosticSeverity;

typedef struct {
    SolarDiagnosticSeverity severity;
    char message[SOLAR_DIAGNOSTIC_MESSAGE_SIZE];
    char hint[SOLAR_DIAGNOSTIC_HINT_SIZE];
} SolarDiagnostic;

void solar_diagnostic_clear(SolarDiagnostic *diagnostic);
void solar_diagnostic_set(
    SolarDiagnostic *diagnostic,
    SolarDiagnosticSeverity severity,
    const char *message,
    const char *hint
);
void solar_diagnostic_format(
    SolarDiagnostic *diagnostic,
    SolarDiagnosticSeverity severity,
    const char *hint,
    const char *format,
    ...
);

#endif

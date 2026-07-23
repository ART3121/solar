#ifndef SOLAR_BACKEND_INTERNAL_H
#define SOLAR_BACKEND_INTERNAL_H

#include "solar/backend.h"

typedef struct {
    const char *name;
    const char *const *version_argv;
    bool optional;
} SolarToolDefinition;

typedef struct SolarBackend {
    const char *name;
    unsigned int capabilities;
    const SolarToolDefinition *tools;
    size_t tool_count;
    size_t elaborate_tool_count;
    SolarResult (*elaborate_request)(
        const SolarRtlBuildRequest *request,
        uint64_t *duration_ns_out
    );
    SolarResult (*simulate)(const SolarProject *project);
    SolarResult (*simulate_request)(
        const SolarSimulationRequest *request,
        SolarSimulationFailureKind *failure_kind_out,
        SolarSimulationMetrics *metrics_out
    );
    SolarResult (*synthesize)(const SolarProject *project);
    SolarResult (*synthesize_request)(
        const SolarSynthesisRequest *request,
        uint64_t *duration_ns_out
    );
} SolarBackend;

extern const SolarBackend SOLAR_IVERILOG_BACKEND;
extern const SolarBackend SOLAR_VERILATOR_BACKEND;
extern const SolarBackend SOLAR_YOSYS_BACKEND;

void solar_backend_add_log_context(
    SolarResult *result,
    const char *tool_name,
    const char *log_path
);

#endif

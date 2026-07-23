#ifndef SOLAR_BACKEND_H
#define SOLAR_BACKEND_H

#include "solar/project.h"
#include "solar/progress.h"
#include "solar/result.h"
#include "solar/rtl.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SOLAR_BACKEND_SIMULATION = 1U << 0,
    SOLAR_BACKEND_SYNTHESIS = 1U << 1
} SolarBackendCapability;

typedef struct {
    char *name;
    char *path;
    char *version;
    bool available;
    bool optional;
} SolarToolReport;

typedef struct {
    char *backend;
    char *project_root;
    char *working_directory;
    char *test_name;
    char *top;
    SolarStringList rtl_sources;
    SolarStringList test_sources;
    SolarStringList include_dirs;
    SolarStringList defines;
    char *executable_path;
    char *waveform_path;
    char *compile_stdout_log;
    char *compile_stderr_log;
    char *run_stdout_log;
    char *run_stderr_log;
    /* Borrowed while the request is executed. */
    const SolarProgressObserver *progress_observer;
} SolarSimulationRequest;

typedef enum {
    SOLAR_SIMULATION_FAILURE_NONE = 0,
    SOLAR_SIMULATION_FAILURE_COMPILE,
    SOLAR_SIMULATION_FAILURE_RUNTIME,
    SOLAR_SIMULATION_FAILURE_LOGICAL
} SolarSimulationFailureKind;

typedef struct {
    uint64_t compile_duration_ns;
    uint64_t run_duration_ns;
    uint64_t total_duration_ns;
} SolarSimulationMetrics;

void solar_simulation_request_init(SolarSimulationRequest *request);
void solar_simulation_request_free(SolarSimulationRequest *request);

typedef struct {
    char *backend;
    char *project_root;
    char *working_directory;
    char *top;
    SolarStringList rtl_sources;
    SolarStringList include_dirs;
    SolarStringList defines;
    char *script_path;
    char *netlist_path;
    char *report_path;
    char *stdout_log;
    char *stderr_log;
} SolarSynthesisRequest;

void solar_synthesis_request_init(SolarSynthesisRequest *request);
void solar_synthesis_request_free(SolarSynthesisRequest *request);

bool solar_backend_supports(
    const char *name,
    SolarBackendCapability capability
);

SolarResult solar_backend_simulate(const SolarProject *project);
SolarResult solar_backend_simulate_request(
    const SolarSimulationRequest *request,
    SolarSimulationFailureKind *failure_kind_out,
    SolarSimulationMetrics *metrics_out
);
SolarResult solar_backend_elaborate_request(
    const SolarRtlBuildRequest *request,
    uint64_t *duration_ns_out
);
SolarResult solar_backend_synthesize(const SolarProject *project);
SolarResult solar_backend_synthesize_request(
    const SolarSynthesisRequest *request,
    uint64_t *duration_ns_out
);

SolarResult solar_backend_doctor(
    SolarToolReport **reports_out,
    size_t *report_count_out
);
SolarResult solar_backend_doctor_project(
    const SolarProject *project,
    bool all_tools,
    SolarToolReport **reports_out,
    size_t *report_count_out
);
void solar_backend_tool_reports_free(SolarToolReport *reports, size_t count);

#endif

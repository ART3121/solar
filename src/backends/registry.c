#include "backend_internal.h"

#include "solar/filesystem.h"
#include "solar/runner.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SOLAR_PATH_COMPONENT_LIMIT ((size_t)1024U * 1024U)

static const char *const GTKWAVE_VERSION_ARGUMENTS[] = {NULL};
static const char *const COCOTB_VERSION_ARGUMENTS[] = {
    "cocotb-config", "--version", NULL
};
static const char *const PYTHON_VERSION_ARGUMENTS[] = {
    "python3", "--version", NULL
};
static const char *const SURFER_VERSION_ARGUMENTS[] = {
    "surfer", "--version", NULL
};

static const SolarToolDefinition OPTIONAL_TOOLS[] = {
    {"gtkwave", GTKWAVE_VERSION_ARGUMENTS, true},
    {"cocotb-config", COCOTB_VERSION_ARGUMENTS, true},
    {"python3", PYTHON_VERSION_ARGUMENTS, true},
    {"surfer", SURFER_VERSION_ARGUMENTS, true}
};

static const SolarBackend *const BACKENDS[] = {
    &SOLAR_IVERILOG_BACKEND,
    &SOLAR_VERILATOR_BACKEND,
    &SOLAR_YOSYS_BACKEND
};

static SolarResult registry_error(
    SolarStatus status,
    const char *hint,
    const char *format,
    ...
)
{
    SolarResult result;
    va_list arguments;

    result.status = status;
    result.diagnostic.severity = SOLAR_DIAGNOSTIC_ERROR;
    va_start(arguments, format);
    (void)vsnprintf(
        result.diagnostic.message,
        sizeof(result.diagnostic.message),
        format,
        arguments
    );
    va_end(arguments);
    (void)snprintf(
        result.diagnostic.hint,
        sizeof(result.diagnostic.hint),
        "%s",
        hint == NULL ? "" : hint
    );
    return result;
}

void solar_backend_add_log_context(
    SolarResult *result,
    const char *tool_name,
    const char *log_path
)
{
    FILE *log;
    char line[256];
    size_t message_length;
    char *newline;

    if (result == NULL || result->status == SOLAR_STATUS_OK ||
        tool_name == NULL || log_path == NULL) {
        return;
    }
    log = fopen(log_path, "r");
    if (log == NULL) {
        return;
    }
    line[0] = '\0';
    while (fgets(line, sizeof(line), log) != NULL) {
        if (line[0] != '\n' && line[0] != '\r' && line[0] != '\0') {
            break;
        }
    }
    (void)fclose(log);
    newline = strpbrk(line, "\r\n");
    if (newline != NULL) {
        *newline = '\0';
    }
    if (line[0] == '\0') {
        return;
    }
    message_length = strlen(result->diagnostic.message);
    if (message_length < sizeof(result->diagnostic.message) - 1U) {
        (void)snprintf(
            result->diagnostic.message + message_length,
            sizeof(result->diagnostic.message) - message_length,
            "\n%s: %s",
            tool_name,
            line
        );
    }
}

static const SolarBackend *find_backend(
    const char *name,
    SolarBackendCapability capability
)
{
    size_t index;

    if (name == NULL) {
        return NULL;
    }
    for (index = 0U; index < sizeof(BACKENDS) / sizeof(BACKENDS[0]); index++) {
        if (strcmp(name, BACKENDS[index]->name) == 0 &&
            (BACKENDS[index]->capabilities & (unsigned int)capability) != 0U) {
            return BACKENDS[index];
        }
    }
    return NULL;
}

bool solar_backend_supports(
    const char *name,
    SolarBackendCapability capability
)
{
    return find_backend(name, capability) != NULL;
}

static SolarResult executable_candidate(
    const char *directory,
    size_t directory_length,
    const char *name,
    char **path_out
)
{
    char *directory_copy;
    char *candidate = NULL;
    char *resolved = NULL;
    struct stat information;
    SolarResult result;

    if (directory_length > SOLAR_PATH_COMPONENT_LIMIT) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "backend executable search path is too long",
            "use a shorter PATH entry"
        );
    }
    if (directory_length == 0U) {
        directory_copy = strdup(".");
    } else {
        directory_copy = malloc(directory_length + 1U);
        if (directory_copy != NULL) {
            (void)memcpy(directory_copy, directory, directory_length);
            directory_copy[directory_length] = '\0';
        }
    }
    if (directory_copy == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate executable search path",
            "free memory and try again"
        );
    }
    result = solar_filesystem_join(directory_copy, name, &candidate);
    free(directory_copy);
    if (result.status != SOLAR_STATUS_OK) {
        return result;
    }
    if (stat(candidate, &information) == 0 && S_ISREG(information.st_mode) &&
        access(candidate, X_OK) == 0) {
        resolved = realpath(candidate, NULL);
        if (resolved == NULL) {
            resolved = candidate;
            candidate = NULL;
        }
        *path_out = resolved;
    }
    free(candidate);
    return solar_result_ok();
}

static SolarResult find_executable(const char *name, char **path_out)
{
    const char *path_environment;
    const char *component;

    *path_out = NULL;
    if (strchr(name, '/') != NULL) {
        return registry_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "backend tool names must not contain path separators",
            "invalid executable name: %s",
            name
        );
    }
    path_environment = getenv("PATH");
    if (path_environment == NULL) {
        return solar_result_ok();
    }

    component = path_environment;
    for (;;) {
        const char *separator = strchr(component, ':');
        size_t length = separator == NULL
            ? strlen(component)
            : (size_t)(separator - component);
        SolarResult result = executable_candidate(
            component, length, name, path_out
        );

        if (result.status != SOLAR_STATUS_OK || *path_out != NULL) {
            return result;
        }
        if (separator == NULL) {
            break;
        }
        component = separator + 1;
    }
    return solar_result_ok();
}

static char *first_output_line(const SolarProcessResult *process)
{
    const char *source = process->stdout_text;
    size_t length;
    char *line;

    if (source == NULL || source[0] == '\0') {
        source = process->stderr_text;
    }
    if (source == NULL || source[0] == '\0') {
        return NULL;
    }
    while (*source == '\n' || *source == '\r' || *source == ' ' || *source == '\t') {
        source++;
    }
    length = strcspn(source, "\r\n");
    line = malloc(length + 1U);
    if (line == NULL) {
        return NULL;
    }
    (void)memcpy(line, source, length);
    line[length] = '\0';
    return line;
}

static void probe_version(
    const SolarToolDefinition *definition,
    SolarToolReport *report
)
{
    SolarProcessResult process;
    SolarProcessSpec specification = {0};
    const char **arguments = NULL;
    size_t argument_count = 0U;
    size_t index;

    if (definition->version_argv == NULL ||
        definition->version_argv[0] == NULL || report->path == NULL) {
        return;
    }
    while (definition->version_argv[argument_count] != NULL) {
        argument_count++;
    }
    arguments = calloc(argument_count + 1U, sizeof(*arguments));
    if (arguments == NULL) {
        return;
    }
    arguments[0] = report->path;
    for (index = 1U; index < argument_count; index++) {
        arguments[index] = definition->version_argv[index];
    }

    specification.executable = report->path;
    specification.argv = arguments;
    specification.working_directory = NULL;
    specification.stdout_log_path = NULL;
    specification.stderr_log_path = NULL;
    solar_process_result_init(&process);
    (void)solar_runner_run(&specification, &process);
    report->version = first_output_line(&process);
    solar_process_result_free(&process);
    free(arguments);
}

static SolarResult fill_tool_report(
    const SolarToolDefinition *definition,
    SolarToolReport *report
)
{
    SolarResult result;

    (void)memset(report, 0, sizeof(*report));
    report->name = strdup(definition->name);
    report->optional = definition->optional;
    if (report->name == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate tool report",
            "free memory and try again"
        );
    }
    result = find_executable(definition->name, &report->path);
    if (result.status != SOLAR_STATUS_OK) {
        return result;
    }
    report->available = report->path != NULL;
    if (report->available) {
        probe_version(definition, report);
    }
    return solar_result_ok();
}

void solar_backend_tool_reports_free(SolarToolReport *reports, size_t count)
{
    size_t index;

    for (index = 0U; index < count; index++) {
        free(reports[index].name);
        free(reports[index].path);
        free(reports[index].version);
    }
    free(reports);
}

static size_t report_count(void)
{
    size_t count = sizeof(OPTIONAL_TOOLS) / sizeof(OPTIONAL_TOOLS[0]);
    size_t backend_index;

    for (backend_index = 0U;
         backend_index < sizeof(BACKENDS) / sizeof(BACKENDS[0]);
         backend_index++) {
        count += BACKENDS[backend_index]->tool_count;
    }
    return count;
}

SolarResult solar_backend_doctor(
    SolarToolReport **reports_out,
    size_t *report_count_out
)
{
    SolarToolReport *reports;
    size_t count;
    size_t output_index = 0U;
    size_t backend_index;
    size_t tool_index;
    SolarResult result = solar_result_ok();

    if (reports_out != NULL) *reports_out = NULL;
    if (report_count_out != NULL) *report_count_out = 0U;
    if (reports_out == NULL || report_count_out == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot report tools without output storage",
            "provide report and count output values"
        );
    }
    count = report_count();
    reports = calloc(count, sizeof(*reports));
    if (reports == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate tool reports",
            "free memory and try again"
        );
    }

    for (backend_index = 0U;
         backend_index < sizeof(BACKENDS) / sizeof(BACKENDS[0]);
         backend_index++) {
        for (tool_index = 0U;
             tool_index < BACKENDS[backend_index]->tool_count;
             tool_index++) {
            result = fill_tool_report(
                &BACKENDS[backend_index]->tools[tool_index],
                &reports[output_index]
            );
            if (result.status != SOLAR_STATUS_OK) goto cleanup;
            output_index++;
        }
    }
    for (tool_index = 0U;
         tool_index < sizeof(OPTIONAL_TOOLS) / sizeof(OPTIONAL_TOOLS[0]);
         tool_index++) {
        result = fill_tool_report(
            &OPTIONAL_TOOLS[tool_index], &reports[output_index]
        );
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        output_index++;
    }

    *reports_out = reports;
    *report_count_out = count;
    return solar_result_ok();

cleanup:
    solar_backend_tool_reports_free(reports, count);
    return result;
}

static bool report_contains(
    const SolarToolReport *reports,
    size_t count,
    const char *name
)
{
    size_t index;

    for (index = 0U; index < count; index++) {
        if (reports[index].name != NULL &&
            strcmp(reports[index].name, name) == 0) {
            return true;
        }
    }
    return false;
}

static SolarResult append_required_tools(
    const SolarBackend *backend,
    SolarToolReport *reports,
    size_t *count
)
{
    size_t index;

    if (backend == NULL) return solar_result_ok();
    for (index = 0U; index < backend->tool_count; index++) {
        SolarToolDefinition required = backend->tools[index];
        SolarResult result;

        if (report_contains(reports, *count, required.name)) continue;
        required.optional = false;
        result = fill_tool_report(&required, &reports[*count]);
        if (result.status != SOLAR_STATUS_OK) return result;
        (*count)++;
    }
    return solar_result_ok();
}

SolarResult solar_backend_doctor_project(
    const SolarProject *project,
    bool all_tools,
    SolarToolReport **reports_out,
    size_t *report_count_out
)
{
    SolarToolReport *reports;
    size_t count = 0U;
    size_t index;
    SolarResult result;

    if (reports_out != NULL) *reports_out = NULL;
    if (report_count_out != NULL) *report_count_out = 0U;
    if (all_tools || project == NULL) {
        return solar_backend_doctor(reports_out, report_count_out);
    }
    if (reports_out == NULL || report_count_out == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot report project tools without output storage",
            "provide report and count output values"
        );
    }
    reports = calloc(report_count(), sizeof(*reports));
    if (reports == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate project tool reports",
            "free memory and try again"
        );
    }
    result = append_required_tools(
        find_backend(
            project->config.simulation.backend, SOLAR_BACKEND_SIMULATION
        ),
        reports, &count
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = append_required_tools(
        find_backend(
            project->config.synthesis.backend, SOLAR_BACKEND_SYNTHESIS
        ),
        reports, &count
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    for (index = 0U; index < project->config.test_count; index++) {
        if (project->config.tests[index].driver != NULL &&
            strcmp(project->config.tests[index].driver, "cocotb") == 0 &&
            !report_contains(reports, count, "cocotb-config")) {
            SolarToolDefinition required = OPTIONAL_TOOLS[1];

            required.optional = false;
            result = fill_tool_report(&required, &reports[count]);
            if (result.status != SOLAR_STATUS_OK) goto cleanup;
            count++;
            required = OPTIONAL_TOOLS[2];
            required.optional = false;
            result = fill_tool_report(&required, &reports[count]);
            if (result.status != SOLAR_STATUS_OK) goto cleanup;
            count++;
        }
    }
    *reports_out = reports;
    *report_count_out = count;
    return solar_result_ok();

cleanup:
    solar_backend_tool_reports_free(reports, count);
    return result;
}

static SolarResult require_backend_tools(const SolarBackend *backend)
{
    size_t index;

    for (index = 0U; index < backend->tool_count; index++) {
        char *path = NULL;
        SolarResult result = find_executable(backend->tools[index].name, &path);

        if (result.status != SOLAR_STATUS_OK) {
            return result;
        }
        if (path == NULL) {
            return registry_error(
                SOLAR_STATUS_TOOL_MISSING,
                "install the selected backend tools and ensure they are on PATH",
                "required executable is unavailable: %s",
                backend->tools[index].name
            );
        }
        free(path);
    }
    return solar_result_ok();
}

SolarResult solar_backend_simulate(const SolarProject *project)
{
    const SolarBackend *backend;
    SolarResult result;

    if (project == NULL || project->config.simulation.backend == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot simulate an unloaded project",
            "load and validate a project first"
        );
    }
    backend = find_backend(
        project->config.simulation.backend, SOLAR_BACKEND_SIMULATION
    );
    if (backend == NULL || backend->simulate == NULL) {
        return registry_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "select a registered simulation backend",
            "unknown simulation backend: %s",
            project->config.simulation.backend
        );
    }
    result = require_backend_tools(backend);
    return result.status == SOLAR_STATUS_OK ? backend->simulate(project) : result;
}

SolarResult solar_backend_simulate_request(
    const SolarSimulationRequest *request,
    SolarSimulationFailureKind *failure_kind_out,
    SolarSimulationMetrics *metrics_out
)
{
    const SolarBackend *backend;
    SolarResult result;

    if (failure_kind_out != NULL) {
        *failure_kind_out = SOLAR_SIMULATION_FAILURE_NONE;
    }
    if (metrics_out != NULL) (void)memset(metrics_out, 0, sizeof(*metrics_out));
    if (request == NULL || request->backend == NULL ||
        failure_kind_out == NULL || metrics_out == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot simulate an incomplete request",
            "select a test and build its effective configuration first"
        );
    }
    backend = find_backend(request->backend, SOLAR_BACKEND_SIMULATION);
    if (backend == NULL || backend->simulate_request == NULL) {
        return registry_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "select a registered simulation backend",
            "unknown simulation backend: %s",
            request->backend
        );
    }
    result = require_backend_tools(backend);
    return result.status == SOLAR_STATUS_OK
        ? backend->simulate_request(request, failure_kind_out, metrics_out)
        : result;
}

SolarResult solar_backend_elaborate_request(
    const SolarRtlBuildRequest *request,
    uint64_t *duration_ns_out
)
{
    const SolarBackend *backend;
    SolarResult result;
    size_t index;

    if (duration_ns_out != NULL) *duration_ns_out = 0U;
    if (request == NULL || request->backend == NULL || duration_ns_out == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot elaborate an incomplete RTL request",
            "build the request through the Solar RTL Service"
        );
    }
    backend = find_backend(request->backend, SOLAR_BACKEND_SIMULATION);
    if (backend == NULL || backend->elaborate_request == NULL) {
        return registry_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "select a simulation backend with RTL elaboration support",
            "backend cannot elaborate RTL: %s", request->backend
        );
    }
    for (index = 0U; index < backend->elaborate_tool_count; index++) {
        char *path = NULL;

        result = find_executable(backend->tools[index].name, &path);
        if (result.status != SOLAR_STATUS_OK) return result;
        if (path == NULL) {
            return registry_error(
                SOLAR_STATUS_TOOL_MISSING,
                "install the selected backend and ensure it is on PATH",
                "required executable is unavailable: %s",
                backend->tools[index].name
            );
        }
        free(path);
    }
    return backend->elaborate_request(request, duration_ns_out);
}

SolarResult solar_backend_synthesize(const SolarProject *project)
{
    const SolarBackend *backend;
    SolarResult result;

    if (project == NULL || project->config.synthesis.backend == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot synthesize an unloaded project",
            "load and validate a project first"
        );
    }
    backend = find_backend(
        project->config.synthesis.backend, SOLAR_BACKEND_SYNTHESIS
    );
    if (backend == NULL || backend->synthesize == NULL) {
        return registry_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "select a registered synthesis backend",
            "unknown synthesis backend: %s",
            project->config.synthesis.backend
        );
    }
    result = require_backend_tools(backend);
    return result.status == SOLAR_STATUS_OK ? backend->synthesize(project) : result;
}

SolarResult solar_backend_synthesize_request(
    const SolarSynthesisRequest *request,
    uint64_t *duration_ns_out
)
{
    const SolarBackend *backend;
    SolarResult result;

    if (duration_ns_out != NULL) *duration_ns_out = 0U;
    if (request == NULL || request->backend == NULL || duration_ns_out == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot synthesize an incomplete request",
            "build the effective synthesis configuration first"
        );
    }
    backend = find_backend(request->backend, SOLAR_BACKEND_SYNTHESIS);
    if (backend == NULL || backend->synthesize_request == NULL) {
        return registry_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "select a registered synthesis backend",
            "unknown synthesis backend: %s",
            request->backend
        );
    }
    result = require_backend_tools(backend);
    return result.status == SOLAR_STATUS_OK
        ? backend->synthesize_request(request, duration_ns_out)
        : result;
}

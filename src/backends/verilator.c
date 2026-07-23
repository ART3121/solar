#include "backend_internal.h"

#include "solar/filesystem.h"
#include "solar/runner.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *const VERILATOR_VERSION_ARGUMENTS[] = {
    "verilator", "--version", NULL
};
static const SolarToolDefinition VERILATOR_TOOLS[] = {
    {"verilator", VERILATOR_VERSION_ARGUMENTS, true}
};

static SolarResult verilator_error(
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

static bool has_suffix(const char *text, const char *suffix)
{
    size_t text_length;
    size_t suffix_length;

    if (text == NULL || suffix == NULL) return false;
    text_length = strlen(text);
    suffix_length = strlen(suffix);
    return text_length >= suffix_length &&
        strcmp(text + text_length - suffix_length, suffix) == 0;
}

static bool contains_whitespace(const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;

    if (cursor == NULL) return false;
    while (*cursor != '\0') {
        if (isspace(*cursor) != 0) return true;
        cursor++;
    }
    return false;
}

static void free_arguments(
    const SolarSimulationRequest *request,
    const char **arguments,
    size_t include_start,
    size_t define_start,
    char *object_directory
)
{
    size_t index;

    if (arguments != NULL) {
        for (index = 0U; index < request->include_dirs.count; index++) {
            free((char *)arguments[include_start + index]);
        }
        for (index = 0U; index < request->defines.count; index++) {
            free((char *)arguments[define_start + index]);
        }
    }
    free(object_directory);
    free(arguments);
}

static char *prefixed_argument(const char *prefix, const char *value)
{
    size_t length = strlen(prefix) + strlen(value) + 1U;
    char *argument = malloc(length);

    if (argument != NULL) {
        (void)snprintf(argument, length, "%s%s", prefix, value);
    }
    return argument;
}

static SolarResult build_arguments(
    const SolarSimulationRequest *request,
    const char ***arguments_out,
    char **object_directory_out,
    size_t *include_start_out,
    size_t *define_start_out
)
{
    size_t fixed_count = 13U;
    size_t count = fixed_count + request->include_dirs.count +
        request->defines.count + request->rtl_sources.count +
        request->test_sources.count;
    const char **arguments = calloc(count + 1U, sizeof(*arguments));
    char *object_directory = NULL;
    size_t output = 0U;
    size_t index;
    SolarResult result;

    *arguments_out = NULL;
    *object_directory_out = NULL;
    if (arguments == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate Verilator arguments",
            "free memory and try again"
        );
    }
    result = solar_filesystem_join(
        request->working_directory, "verilator-obj", &object_directory
    );
    if (result.status != SOLAR_STATUS_OK) {
        free(arguments);
        return result;
    }
    arguments[output++] = "verilator";
    arguments[output++] = "--binary";
    arguments[output++] = "--timing";
    arguments[output++] = has_suffix(request->waveform_path, ".fst")
        ? "--trace-fst"
        : "--trace";
    arguments[output++] = "--autoflush";
    /* Verilator still reports warnings, but they must not abort a valid test. */
    arguments[output++] = "-Wno-fatal";
    arguments[output++] = "--top-module";
    arguments[output++] = request->top;
    arguments[output++] = "--Mdir";
    arguments[output++] = object_directory;
    arguments[output++] = "-o";
    arguments[output++] = request->executable_path;
    arguments[output++] = "--quiet-build";

    *include_start_out = output;
    *define_start_out = output + request->include_dirs.count;
    for (index = 0U; index < request->include_dirs.count; index++) {
        arguments[output] = prefixed_argument(
            "-I", request->include_dirs.items[index]
        );
        if (arguments[output++] == NULL) goto allocation_failure;
    }
    for (index = 0U; index < request->defines.count; index++) {
        arguments[output] = prefixed_argument(
            "-D", request->defines.items[index]
        );
        if (arguments[output++] == NULL) goto allocation_failure;
    }
    for (index = 0U; index < request->rtl_sources.count; index++) {
        arguments[output++] = request->rtl_sources.items[index];
    }
    for (index = 0U; index < request->test_sources.count; index++) {
        arguments[output++] = request->test_sources.items[index];
    }
    *arguments_out = arguments;
    *object_directory_out = object_directory;
    return solar_result_ok();

allocation_failure:
    free_arguments(
        request, arguments, *include_start_out, *define_start_out,
        object_directory
    );
    return solar_result_error(
        SOLAR_STATUS_INTERNAL_ERROR,
        "could not allocate a Verilator option",
        "free memory and try again"
    );
}

static SolarResult run_compile(
    const SolarSimulationRequest *request,
    uint64_t *duration_ns_out
)
{
    const char **arguments = NULL;
    char *object_directory = NULL;
    size_t include_start = 0U;
    size_t define_start = 0U;
    SolarProcessSpec specification = {0};
    SolarProcessResult process;
    SolarResult result = build_arguments(
        request, &arguments, &object_directory, &include_start, &define_start
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    specification.executable = "verilator";
    specification.argv = arguments;
    specification.working_directory = request->working_directory;
    specification.stdout_log_path = request->compile_stdout_log;
    specification.stderr_log_path = request->compile_stderr_log;
    specification.progress_observer = request->progress_observer;
    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    *duration_ns_out = process.duration_ns;
    solar_process_result_free(&process);
    free_arguments(
        request, arguments, include_start, define_start, object_directory
    );
    if (result.status != SOLAR_STATUS_OK) {
        solar_backend_add_log_context(
            &result, "verilator", request->compile_stderr_log
        );
        (void)snprintf(
            result.diagnostic.hint,
            sizeof(result.diagnostic.hint),
            "inspect Verilator stderr at %s",
            request->compile_stderr_log
        );
    }
    return result;
}

static SolarResult run_binary(
    const SolarSimulationRequest *request,
    bool *nonzero_exit_out,
    uint64_t *duration_ns_out
)
{
    const char *arguments[] = {request->executable_path, NULL};
    SolarProcessSpec specification = {
        request->executable_path, arguments, request->working_directory,
        request->run_stdout_log, request->run_stderr_log, NULL, 0U,
        request->progress_observer
    };
    SolarProcessResult process;
    SolarResult result;

    *nonzero_exit_out = false;
    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    *duration_ns_out = process.duration_ns;
    if (process.outcome == SOLAR_PROCESS_EXITED && process.exit_code != 0) {
        *nonzero_exit_out = true;
    }
    solar_process_result_free(&process);
    if (result.status != SOLAR_STATUS_OK) {
        solar_backend_add_log_context(
            &result, "simulation", request->run_stderr_log
        );
    }
    return result;
}

static SolarResult verify_executable(const SolarSimulationRequest *request)
{
    struct stat information;

    if (stat(request->executable_path, &information) != 0 ||
        !S_ISREG(information.st_mode) || access(request->executable_path, X_OK) != 0) {
        return verilator_error(
            SOLAR_STATUS_PROCESS_FAILED,
            "inspect the Verilator compile logs",
            "Verilator did not create executable %s",
            request->executable_path
        );
    }
    return solar_result_ok();
}

static SolarResult verilator_simulate_request(
    const SolarSimulationRequest *request,
    SolarSimulationFailureKind *failure_kind_out,
    SolarSimulationMetrics *metrics_out
)
{
    SolarResult result;
    bool nonzero_exit = false;

    *failure_kind_out = SOLAR_SIMULATION_FAILURE_NONE;
    (void)memset(metrics_out, 0, sizeof(*metrics_out));
    if (request->working_directory == NULL || request->test_name == NULL ||
        request->top == NULL || request->executable_path == NULL ||
        request->waveform_path == NULL || request->compile_stdout_log == NULL ||
        request->compile_stderr_log == NULL || request->run_stdout_log == NULL ||
        request->run_stderr_log == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "Verilator received an incomplete simulation request",
            "build the request through the Solar Test Service"
        );
    }
    if (!has_suffix(request->waveform_path, ".vcd") &&
        !has_suffix(request->waveform_path, ".fst")) {
        return verilator_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "use a waveform name ending in .vcd or .fst",
            "Verilator does not support the configured waveform extension: %s",
            request->waveform_path
        );
    }
    /* Verilator's generated GNU Make files explicitly reject whitespace in
     * CURDIR. Keeping the workspace below .solar is more important than
     * silently staging project data in a global temporary directory. */
    if (contains_whitespace(request->working_directory)) {
        return verilator_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "move the project to a path without spaces or select iverilog",
            "Verilator cannot build simulations in a project path containing whitespace: %s",
            request->working_directory
        );
    }
    if (unlink(request->executable_path) != 0 && errno != ENOENT) {
        return verilator_error(
            SOLAR_STATUS_IO_ERROR,
            "ensure the test artifact directory is writable",
            "cannot reset Verilator executable %s: %s",
            request->executable_path,
            strerror(errno)
        );
    }
    if (unlink(request->waveform_path) != 0 && errno != ENOENT) {
        return verilator_error(
            SOLAR_STATUS_IO_ERROR,
            "ensure the test artifact directory is writable",
            "cannot reset waveform %s: %s",
            request->waveform_path,
            strerror(errno)
        );
    }
    solar_progress_stage(
        request->progress_observer, SOLAR_PROGRESS_SIMULATION_COMPILE,
        "Compiling with Verilator", "Verilator"
    );
    result = run_compile(request, &metrics_out->compile_duration_ns);
    metrics_out->total_duration_ns = metrics_out->compile_duration_ns;
    if (result.status != SOLAR_STATUS_OK) {
        *failure_kind_out = SOLAR_SIMULATION_FAILURE_COMPILE;
        return result;
    }
    result = verify_executable(request);
    if (result.status != SOLAR_STATUS_OK) {
        *failure_kind_out = SOLAR_SIMULATION_FAILURE_COMPILE;
        return result;
    }
    solar_progress_stage(
        request->progress_observer, SOLAR_PROGRESS_SIMULATION_RUN,
        "Running simulation", "Verilator"
    );
    result = run_binary(request, &nonzero_exit, &metrics_out->run_duration_ns);
    metrics_out->total_duration_ns = metrics_out->compile_duration_ns +
        metrics_out->run_duration_ns;
    if (result.status != SOLAR_STATUS_OK) {
        *failure_kind_out = nonzero_exit
            ? SOLAR_SIMULATION_FAILURE_LOGICAL
            : SOLAR_SIMULATION_FAILURE_RUNTIME;
        return result;
    }
    return solar_result_ok();
}

static SolarResult verilator_elaborate_request(
    const SolarRtlBuildRequest *request,
    uint64_t *duration_ns_out
)
{
    size_t count;
    const char **arguments;
    size_t include_start = 0U;
    size_t define_start = 0U;
    size_t includes_allocated = 0U;
    size_t defines_allocated = 0U;
    size_t output = 0U;
    size_t index;
    SolarProcessSpec specification = {0};
    SolarProcessResult process;
    SolarResult result = solar_result_ok();

    if (request->working_directory == NULL || request->top == NULL ||
        request->rtl_sources.count == 0U || request->stdout_log == NULL ||
        request->stderr_log == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "Verilator received an incomplete RTL build request",
            "build the request through the Solar RTL Service"
        );
    }
    count = 5U + request->include_dirs.count +
        request->defines.count + request->rtl_sources.count;
    arguments = calloc(count + 1U, sizeof(*arguments));
    if (arguments == NULL) return solar_result_error(
        SOLAR_STATUS_INTERNAL_ERROR,
        "could not allocate Verilator RTL arguments",
        "free memory and try again"
    );
    arguments[output++] = "verilator";
    arguments[output++] = "--lint-only";
    arguments[output++] = "--timing";
    arguments[output++] = "--top-module";
    arguments[output++] = request->top;
    include_start = output;
    for (index = 0U; index < request->include_dirs.count; index++) {
        arguments[output] = prefixed_argument(
            "-I", request->include_dirs.items[index]
        );
        if (arguments[output++] == NULL) goto allocation_failure;
        includes_allocated++;
    }
    define_start = output;
    for (index = 0U; index < request->defines.count; index++) {
        arguments[output] = prefixed_argument(
            "-D", request->defines.items[index]
        );
        if (arguments[output++] == NULL) goto allocation_failure;
        defines_allocated++;
    }
    for (index = 0U; index < request->rtl_sources.count; index++) {
        arguments[output++] = request->rtl_sources.items[index];
    }
    specification.executable = "verilator";
    specification.argv = arguments;
    specification.working_directory = request->working_directory;
    specification.stdout_log_path = request->stdout_log;
    specification.stderr_log_path = request->stderr_log;
    specification.progress_observer = request->progress_observer;
    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    *duration_ns_out = process.duration_ns;
    solar_process_result_free(&process);
    if (result.status != SOLAR_STATUS_OK) {
        solar_backend_add_log_context(&result, "verilator", request->stderr_log);
    }
    goto cleanup;

allocation_failure:
    result = solar_result_error(
        SOLAR_STATUS_INTERNAL_ERROR,
        "could not allocate a Verilator RTL option",
        "free memory and try again"
    );

cleanup:
    for (index = 0U; index < includes_allocated; index++) {
        free((char *)arguments[include_start + index]);
    }
    for (index = 0U; index < defines_allocated; index++) {
        free((char *)arguments[define_start + index]);
    }
    free(arguments);
    return result;
}

const SolarBackend SOLAR_VERILATOR_BACKEND = {
    .name = "verilator",
    .capabilities = SOLAR_BACKEND_SIMULATION,
    .tools = VERILATOR_TOOLS,
    .tool_count = sizeof(VERILATOR_TOOLS) / sizeof(VERILATOR_TOOLS[0]),
    .elaborate_tool_count = 1U,
    .elaborate_request = verilator_elaborate_request,
    .simulate = NULL,
    .simulate_request = verilator_simulate_request,
    .synthesize = NULL,
    .synthesize_request = NULL
};

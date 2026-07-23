#include "backend_internal.h"

#include "solar/filesystem.h"
#include "solar/runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *executable;
    char *compile_stdout;
    char *compile_stderr;
    char *run_stdout;
    char *run_stderr;
    char *waveform;
} IcarusPaths;

static const char *const IVERILOG_VERSION_ARGUMENTS[] = {
    "iverilog", "-V", NULL
};
static const char *const VVP_VERSION_ARGUMENTS[] = {
    "vvp", "-V", NULL
};
static const SolarToolDefinition ICARUS_TOOLS[] = {
    {"iverilog", IVERILOG_VERSION_ARGUMENTS, false},
    {"vvp", VVP_VERSION_ARGUMENTS, false}
};

static void icarus_paths_free(IcarusPaths *paths)
{
    free(paths->executable);
    free(paths->compile_stdout);
    free(paths->compile_stderr);
    free(paths->run_stdout);
    free(paths->run_stderr);
    free(paths->waveform);
    (void)memset(paths, 0, sizeof(*paths));
}

static SolarResult resolve_icarus_paths(
    const SolarProject *project,
    IcarusPaths *paths
)
{
    SolarResult result;

    (void)memset(paths, 0, sizeof(*paths));
    result = solar_project_resolve_path(
        project, ".solar/tmp/tests/default/simulation.vvp", &paths->executable
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_project_resolve_path(
        project, ".solar/logs/iverilog.stdout.log", &paths->compile_stdout
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_project_resolve_path(
        project, ".solar/logs/iverilog.stderr.log", &paths->compile_stderr
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_project_resolve_path(
        project, ".solar/logs/vvp.stdout.log", &paths->run_stdout
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_project_resolve_path(
        project, ".solar/logs/vvp.stderr.log", &paths->run_stderr
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    return solar_project_resolve_path(
        project, project->config.simulation.waveform, &paths->waveform
    );
}

static void free_compile_arguments(
    const SolarProject *project,
    const char **arguments
)
{
    size_t source_count = project->config.sources.rtl.count +
        project->config.sources.testbench.count;
    size_t index;

    if (arguments == NULL) {
        return;
    }
    for (index = 0U; index < source_count; index++) {
        free((char *)arguments[6U + index]);
    }
    free(arguments);
}

static SolarResult build_compile_arguments(
    const SolarProject *project,
    const IcarusPaths *paths,
    const char ***arguments_out
)
{
    size_t rtl_count = project->config.sources.rtl.count;
    size_t testbench_count = project->config.sources.testbench.count;
    size_t source_count = rtl_count + testbench_count;
    const char **arguments;
    size_t index;
    SolarResult result;

    *arguments_out = NULL;
    arguments = calloc(7U + source_count, sizeof(*arguments));
    if (arguments == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate Icarus arguments",
            "free memory and try again"
        );
    }
    arguments[0] = "iverilog";
    arguments[1] = "-g2012";
    arguments[2] = "-o";
    arguments[3] = paths->executable;
    arguments[4] = "-s";
    arguments[5] = project->config.simulation.top;

    for (index = 0U; index < rtl_count; index++) {
        char *resolved = NULL;
        result = solar_project_resolve_path(
            project, project->config.sources.rtl.items[index], &resolved
        );
        if (result.status != SOLAR_STATUS_OK) {
            free_compile_arguments(project, arguments);
            return result;
        }
        arguments[6U + index] = resolved;
    }
    for (index = 0U; index < testbench_count; index++) {
        char *resolved = NULL;
        result = solar_project_resolve_path(
            project, project->config.sources.testbench.items[index], &resolved
        );
        if (result.status != SOLAR_STATUS_OK) {
            free_compile_arguments(project, arguments);
            return result;
        }
        arguments[6U + rtl_count + index] = resolved;
    }
    *arguments_out = arguments;
    return solar_result_ok();
}

static SolarResult run_iverilog(
    const SolarProject *project,
    const IcarusPaths *paths
)
{
    const char **arguments = NULL;
    SolarProcessResult process;
    SolarProcessSpec specification = {0};
    SolarResult result = build_compile_arguments(project, paths, &arguments);

    if (result.status != SOLAR_STATUS_OK) {
        free_compile_arguments(project, arguments);
        return result;
    }
    specification.executable = "iverilog";
    specification.argv = arguments;
    specification.working_directory = project->root;
    specification.stdout_log_path = paths->compile_stdout;
    specification.stderr_log_path = paths->compile_stderr;
    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    solar_process_result_free(&process);
    free_compile_arguments(project, arguments);
    if (result.status != SOLAR_STATUS_OK) {
        solar_backend_add_log_context(&result, "iverilog", paths->compile_stderr);
        (void)snprintf(
            result.diagnostic.hint,
            sizeof(result.diagnostic.hint),
            "inspect Icarus stderr at %s",
            paths->compile_stderr
        );
    }
    return result;
}

static SolarResult run_vvp(
    const SolarProject *project,
    const IcarusPaths *paths
)
{
    const char *arguments[] = {"vvp", paths->executable, NULL};
    SolarProcessSpec specification = {
        "vvp", arguments, project->root, paths->run_stdout, paths->run_stderr,
        NULL, 0U, NULL
    };
    SolarProcessResult process;
    SolarResult result;

    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    solar_process_result_free(&process);
    if (result.status != SOLAR_STATUS_OK) {
        solar_backend_add_log_context(&result, "vvp", paths->run_stderr);
        (void)snprintf(
            result.diagnostic.hint,
            sizeof(result.diagnostic.hint),
            "inspect simulation stderr at %s",
            paths->run_stderr
        );
    }
    return result;
}

static void free_request_arguments(
    const SolarSimulationRequest *request,
    const char **arguments
)
{
    size_t define_start = 6U + (request->include_dirs.count * 2U);
    size_t index;

    if (arguments == NULL) return;
    for (index = 0U; index < request->defines.count; index++) {
        free((char *)arguments[define_start + index]);
    }
    free(arguments);
}

static SolarResult build_request_arguments(
    const SolarSimulationRequest *request,
    const char ***arguments_out
)
{
    size_t argument_count = 6U + (request->include_dirs.count * 2U) +
        request->defines.count + request->rtl_sources.count +
        request->test_sources.count;
    const char **arguments = calloc(argument_count + 1U, sizeof(*arguments));
    size_t output_index = 0U;
    size_t index;

    *arguments_out = NULL;
    if (arguments == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate Icarus arguments",
            "free memory and try again"
        );
    }
    arguments[output_index++] = "iverilog";
    arguments[output_index++] = "-g2012";
    arguments[output_index++] = "-o";
    arguments[output_index++] = request->executable_path;
    arguments[output_index++] = "-s";
    arguments[output_index++] = request->top;

    for (index = 0U; index < request->include_dirs.count; index++) {
        arguments[output_index++] = "-I";
        arguments[output_index++] = request->include_dirs.items[index];
    }
    for (index = 0U; index < request->defines.count; index++) {
        size_t length = strlen(request->defines.items[index]);
        char *define_argument = malloc(length + 3U);

        if (define_argument == NULL) {
            free_request_arguments(request, arguments);
            return solar_result_error(
                SOLAR_STATUS_INTERNAL_ERROR,
                "could not allocate an Icarus define argument",
                "free memory and try again"
            );
        }
        (void)snprintf(define_argument, length + 3U, "-D%s",
            request->defines.items[index]);
        arguments[output_index++] = define_argument;
    }
    for (index = 0U; index < request->rtl_sources.count; index++) {
        arguments[output_index++] = request->rtl_sources.items[index];
    }
    for (index = 0U; index < request->test_sources.count; index++) {
        arguments[output_index++] = request->test_sources.items[index];
    }
    *arguments_out = arguments;
    return solar_result_ok();
}

static SolarResult run_request_iverilog(
    const SolarSimulationRequest *request,
    uint64_t *duration_ns_out
)
{
    const char **arguments = NULL;
    SolarProcessResult process;
    SolarProcessSpec specification = {0};
    SolarResult result = build_request_arguments(request, &arguments);

    if (result.status != SOLAR_STATUS_OK) {
        free_request_arguments(request, arguments);
        return result;
    }
    specification.executable = "iverilog";
    specification.argv = arguments;
    specification.working_directory = request->working_directory;
    specification.stdout_log_path = request->compile_stdout_log;
    specification.stderr_log_path = request->compile_stderr_log;
    specification.progress_observer = request->progress_observer;
    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    *duration_ns_out = process.duration_ns;
    solar_process_result_free(&process);
    free_request_arguments(request, arguments);
    if (result.status != SOLAR_STATUS_OK) {
        solar_backend_add_log_context(
            &result, "iverilog", request->compile_stderr_log
        );
        (void)snprintf(
            result.diagnostic.hint,
            sizeof(result.diagnostic.hint),
            "inspect Icarus stderr at %s",
            request->compile_stderr_log
        );
    }
    return result;
}

static SolarResult run_request_vvp(
    const SolarSimulationRequest *request,
    bool *nonzero_exit_out,
    uint64_t *duration_ns_out
)
{
    const char *arguments[] = {"vvp", request->executable_path, NULL};
    SolarProcessSpec specification = {
        "vvp", arguments, request->working_directory,
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
        solar_backend_add_log_context(&result, "vvp", request->run_stderr_log);
        (void)snprintf(
            result.diagnostic.hint,
            sizeof(result.diagnostic.hint),
            "inspect simulation stderr at %s",
            request->run_stderr_log
        );
    }
    return result;
}

static SolarResult icarus_simulate_request(
    const SolarSimulationRequest *request,
    SolarSimulationFailureKind *failure_kind_out,
    SolarSimulationMetrics *metrics_out
)
{
    SolarResult result;
    bool nonzero_runtime_exit = false;

    *failure_kind_out = SOLAR_SIMULATION_FAILURE_NONE;
    (void)memset(metrics_out, 0, sizeof(*metrics_out));

    if (request->project_root == NULL || request->working_directory == NULL ||
        request->test_name == NULL || request->top == NULL ||
        request->executable_path == NULL || request->waveform_path == NULL ||
        request->compile_stdout_log == NULL ||
        request->compile_stderr_log == NULL || request->run_stdout_log == NULL ||
        request->run_stderr_log == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "Icarus received an incomplete simulation request",
            "build the request through the Solar Test Service"
        );
    }
    solar_progress_stage(
        request->progress_observer, SOLAR_PROGRESS_SIMULATION_COMPILE,
        "Compiling with Icarus", "Icarus Verilog"
    );
    result = run_request_iverilog(request, &metrics_out->compile_duration_ns);
    metrics_out->total_duration_ns = metrics_out->compile_duration_ns;
    if (result.status != SOLAR_STATUS_OK) {
        *failure_kind_out = SOLAR_SIMULATION_FAILURE_COMPILE;
        return result;
    }
    solar_progress_stage(
        request->progress_observer, SOLAR_PROGRESS_SIMULATION_RUN,
        "Running simulation", "VVP"
    );
    result = run_request_vvp(
        request, &nonzero_runtime_exit, &metrics_out->run_duration_ns
    );
    metrics_out->total_duration_ns = metrics_out->compile_duration_ns +
        metrics_out->run_duration_ns;
    if (result.status != SOLAR_STATUS_OK) {
        *failure_kind_out = nonzero_runtime_exit
            ? SOLAR_SIMULATION_FAILURE_LOGICAL
            : SOLAR_SIMULATION_FAILURE_RUNTIME;
        return result;
    }
    return solar_result_ok();
}

static SolarResult icarus_simulate(const SolarProject *project)
{
    IcarusPaths paths;
    SolarResult result = solar_filesystem_prepare_layout(project->root);

    if (result.status != SOLAR_STATUS_OK) {
        return result;
    }
    result = resolve_icarus_paths(project, &paths);
    if (result.status != SOLAR_STATUS_OK) {
        icarus_paths_free(&paths);
        return result;
    }
    result = solar_filesystem_reset_artifact(
        project->root, ".solar/tmp/tests/default/simulation.vvp"
    );
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_filesystem_reset_artifact(
            project->root, project->config.simulation.waveform
        );
    }
    if (result.status != SOLAR_STATUS_OK) {
        icarus_paths_free(&paths);
        return result;
    }
    result = run_iverilog(project, &paths);
    if (result.status == SOLAR_STATUS_OK) {
        result = run_vvp(project, &paths);
    }
    icarus_paths_free(&paths);
    return result;
}

static SolarResult icarus_elaborate_request(
    const SolarRtlBuildRequest *request,
    uint64_t *duration_ns_out
)
{
    size_t count;
    const char **arguments;
    size_t define_start;
    size_t output = 0U;
    size_t index;
    SolarProcessSpec specification = {0};
    SolarProcessResult process;
    SolarResult result;

    if (request->working_directory == NULL || request->top == NULL ||
        request->rtl_sources.count == 0U || request->stdout_log == NULL ||
        request->stderr_log == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "Icarus received an incomplete RTL build request",
            "build the request through the Solar RTL Service"
        );
    }
    count = 5U + request->include_dirs.count * 2U +
        request->defines.count + request->rtl_sources.count;
    arguments = calloc(count + 1U, sizeof(*arguments));
    if (arguments == NULL) return solar_result_error(
        SOLAR_STATUS_INTERNAL_ERROR,
        "could not allocate Icarus RTL arguments",
        "free memory and try again"
    );
    arguments[output++] = "iverilog";
    arguments[output++] = "-g2012";
    arguments[output++] = "-tnull";
    arguments[output++] = "-s";
    arguments[output++] = request->top;
    for (index = 0U; index < request->include_dirs.count; index++) {
        arguments[output++] = "-I";
        arguments[output++] = request->include_dirs.items[index];
    }
    define_start = output;
    for (index = 0U; index < request->defines.count; index++) {
        size_t length = strlen(request->defines.items[index]);
        char *define = malloc(length + 3U);

        if (define == NULL) {
            result = solar_result_error(
                SOLAR_STATUS_INTERNAL_ERROR,
                "could not allocate an Icarus RTL define",
                "free memory and try again"
            );
            goto cleanup;
        }
        (void)snprintf(define, length + 3U, "-D%s",
            request->defines.items[index]);
        arguments[output++] = define;
    }
    for (index = 0U; index < request->rtl_sources.count; index++) {
        arguments[output++] = request->rtl_sources.items[index];
    }
    specification.executable = "iverilog";
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
        solar_backend_add_log_context(&result, "iverilog", request->stderr_log);
    }

cleanup:
    for (index = 0U; index < request->defines.count; index++) {
        free((char *)arguments[define_start + index]);
    }
    free(arguments);
    return result;
}

const SolarBackend SOLAR_IVERILOG_BACKEND = {
    .name = "iverilog",
    .capabilities = SOLAR_BACKEND_SIMULATION,
    .tools = ICARUS_TOOLS,
    .tool_count = sizeof(ICARUS_TOOLS) / sizeof(ICARUS_TOOLS[0]),
    .elaborate_tool_count = 1U,
    .elaborate_request = icarus_elaborate_request,
    .simulate = icarus_simulate,
    .simulate_request = icarus_simulate_request,
    .synthesize = NULL,
    .synthesize_request = NULL
};

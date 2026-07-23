#include "solar/cocotb.h"

#include "solar/filesystem.h"
#include "solar/runner.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static char *self_executable_path(void)
{
    size_t capacity = 256U;

    for (;;) {
        char *path = malloc(capacity);
        ssize_t length;

        if (path == NULL) return NULL;
        length = readlink("/proc/self/exe", path, capacity - 1U);
        if (length < 0) {
            free(path);
            return NULL;
        }
        if ((size_t)length < capacity - 1U) {
            path[length] = '\0';
            return path;
        }
        free(path);
        if (capacity > SIZE_MAX / 2U) return NULL;
        capacity *= 2U;
    }
}

static char *find_runner_script(void)
{
    static const char *const RELATIVE_CANDIDATES[] = {
        "cocotb/solar_cocotb_runner.py",
        SOLAR_COCOTB_INSTALL_RELATIVE
    };
    char *executable = self_executable_path();
    char *separator;
    size_t index;

    if (executable == NULL) return NULL;
    separator = strrchr(executable, '/');
    if (separator == NULL) {
        free(executable);
        return NULL;
    }
    *separator = '\0';
    for (index = 0U;
         index < sizeof(RELATIVE_CANDIDATES) /
             sizeof(RELATIVE_CANDIDATES[0]);
         index++) {
        char *candidate = NULL;
        struct stat information;
        SolarResult result = solar_filesystem_join(
            executable, RELATIVE_CANDIDATES[index], &candidate
        );

        if (result.status != SOLAR_STATUS_OK) continue;
        if (stat(candidate, &information) == 0 &&
            S_ISREG(information.st_mode) && access(candidate, R_OK) == 0) {
            free(executable);
            return candidate;
        }
        free(candidate);
    }
    free(executable);
    return NULL;
}

static SolarResult build_arguments(
    const SolarSimulationRequest *request,
    const char *module_path,
    const char *script_path,
    const char *timing_path,
    const char ***arguments_out
)
{
    size_t pair_count = request->rtl_sources.count +
        request->test_sources.count + request->include_dirs.count +
        request->defines.count;
    const char **arguments;
    size_t output = 0U;
    size_t index;

    *arguments_out = NULL;
    if (pair_count > (SIZE_MAX - 13U) / 2U) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "too many cocotb arguments to represent",
            "reduce the number of sources, includes, or defines"
        );
    }
    arguments = calloc(13U + (pair_count * 2U), sizeof(*arguments));
    if (arguments == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate cocotb runner arguments",
            "free memory and try again"
        );
    }
    arguments[output++] = "python3";
    arguments[output++] = script_path;
    arguments[output++] = "--top";
    arguments[output++] = request->top;
    arguments[output++] = "--build-dir";
    arguments[output++] = request->working_directory;
    arguments[output++] = "--test-file";
    arguments[output++] = module_path;
    arguments[output++] = "--waveform";
    arguments[output++] = request->waveform_path;
    arguments[output++] = "--timing-file";
    arguments[output++] = timing_path;
    for (index = 0U; index < request->rtl_sources.count; index++) {
        arguments[output++] = "--source";
        arguments[output++] = request->rtl_sources.items[index];
    }
    for (index = 0U; index < request->test_sources.count; index++) {
        arguments[output++] = "--source";
        arguments[output++] = request->test_sources.items[index];
    }
    for (index = 0U; index < request->include_dirs.count; index++) {
        arguments[output++] = "--include";
        arguments[output++] = request->include_dirs.items[index];
    }
    for (index = 0U; index < request->defines.count; index++) {
        arguments[output++] = "--define";
        arguments[output++] = request->defines.items[index];
    }
    *arguments_out = arguments;
    return solar_result_ok();
}

static void read_timings(const char *path, SolarSimulationMetrics *metrics)
{
    FILE *file = fopen(path, "r");
    uint64_t compile_ns = 0U;
    uint64_t simulation_ns = 0U;

    if (file == NULL) return;
    if (fscanf(
            file,
            "compile_ns=%" SCNu64 "\nsimulation_ns=%" SCNu64,
            &compile_ns,
            &simulation_ns
        ) == 2) {
        metrics->compile_duration_ns = compile_ns;
        metrics->run_duration_ns = simulation_ns;
    }
    (void)fclose(file);
}

static SolarResult process_failure(
    const SolarSimulationRequest *request,
    const SolarProcessResult *process,
    SolarSimulationFailureKind *failure_kind_out
)
{
    SolarStatus status = SOLAR_STATUS_PROCESS_FAILED;
    const char *message = "cocotb execution failed";
    char hint[512];

    if (process->outcome == SOLAR_PROCESS_EXEC_FAILED ||
        (process->outcome == SOLAR_PROCESS_EXITED && process->exit_code == 4)) {
        status = SOLAR_STATUS_TOOL_MISSING;
        message = "Python 3 or cocotb 2.x is unavailable";
        *failure_kind_out = SOLAR_SIMULATION_FAILURE_COMPILE;
    } else if (process->outcome == SOLAR_PROCESS_EXITED &&
               process->exit_code == 5) {
        message = "cocotb/Verilator build failed";
        *failure_kind_out = SOLAR_SIMULATION_FAILURE_COMPILE;
    } else if (process->outcome == SOLAR_PROCESS_EXITED &&
               process->exit_code == 6) {
        message = "cocotb test failed";
        *failure_kind_out = SOLAR_SIMULATION_FAILURE_LOGICAL;
    } else {
        *failure_kind_out = SOLAR_SIMULATION_FAILURE_RUNTIME;
    }
    (void)snprintf(
        hint, sizeof(hint), "inspect the original tool output in %s",
        request->run_stderr_log
    );
    return solar_result_error(status, message, hint);
}

SolarResult solar_cocotb_run(
    const SolarSimulationRequest *request,
    const char *test_module_path,
    SolarSimulationFailureKind *failure_kind_out,
    SolarSimulationMetrics *metrics_out
)
{
    char *script = NULL;
    char *timing_path = NULL;
    const char **arguments = NULL;
    SolarProcessSpec specification = {0};
    SolarProcessResult process;
    SolarResult result;

    if (failure_kind_out != NULL) {
        *failure_kind_out = SOLAR_SIMULATION_FAILURE_NONE;
    }
    if (metrics_out != NULL) (void)memset(metrics_out, 0, sizeof(*metrics_out));
    if (request == NULL || test_module_path == NULL ||
        failure_kind_out == NULL || metrics_out == NULL || request->top == NULL ||
        request->working_directory == NULL || request->waveform_path == NULL ||
        request->run_stdout_log == NULL || request->run_stderr_log == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot run an incomplete cocotb request",
            "build the request through the Solar Test Service"
        );
    }
    if (contains_whitespace(request->working_directory)) {
        return solar_result_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "cocotb/Verilator cannot build in a project path containing whitespace",
            "move the project to a path without spaces"
        );
    }
    script = find_runner_script();
    if (script == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "Solar's cocotb adapter is unavailable",
            "rebuild or reinstall Solar"
        );
    }
    result = solar_filesystem_join(
        request->working_directory, "cocotb-timing.txt", &timing_path
    );
    if (result.status == SOLAR_STATUS_OK) (void)unlink(timing_path);
    if (result.status == SOLAR_STATUS_OK) {
        result = build_arguments(
            request, test_module_path, script, timing_path, &arguments
        );
    }
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    specification.executable = "python3";
    specification.argv = arguments;
    specification.working_directory = request->working_directory;
    specification.stdout_log_path = request->run_stdout_log;
    specification.stderr_log_path = request->run_stderr_log;
    specification.progress_observer = request->progress_observer;
    solar_progress_stage(
        request->progress_observer, SOLAR_PROGRESS_SIMULATION_RUN,
        "Running cocotb simulation", "cocotb / Verilator"
    );
    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    metrics_out->total_duration_ns = process.duration_ns;
    read_timings(timing_path, metrics_out);
    if (result.status != SOLAR_STATUS_OK) {
        if (process.outcome == SOLAR_PROCESS_NOT_STARTED) {
            *failure_kind_out = SOLAR_SIMULATION_FAILURE_RUNTIME;
        } else {
            result = process_failure(request, &process, failure_kind_out);
        }
    }
    solar_process_result_free(&process);

cleanup:
    free(timing_path);
    free(arguments);
    free(script);
    return result;
}

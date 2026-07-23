#include "backend_internal.h"

#include "solar/filesystem.h"
#include "solar/runner.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    char *script;
    char *netlist;
    char *report;
    char *stdout_log;
    char *stderr_log;
} YosysPaths;

static const char *const YOSYS_VERSION_ARGUMENTS[] = {
    "yosys", "-V", NULL
};
static const SolarToolDefinition YOSYS_TOOLS[] = {
    {"yosys", YOSYS_VERSION_ARGUMENTS, false}
};

static SolarResult yosys_error(
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

static void yosys_paths_free(YosysPaths *paths)
{
    free(paths->script);
    free(paths->netlist);
    free(paths->report);
    free(paths->stdout_log);
    free(paths->stderr_log);
    (void)memset(paths, 0, sizeof(*paths));
}

static SolarResult resolve_yosys_paths(
    const SolarProject *project,
    YosysPaths *paths
)
{
    SolarResult result;

    (void)memset(paths, 0, sizeof(*paths));
    result = solar_project_resolve_path(
        project, ".solar/tmp/synth/synthesis.ys", &paths->script
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_project_resolve_path(
        project, ".solar/tmp/synth/netlist.v", &paths->netlist
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_project_resolve_path(
        project, ".solar/tmp/synth/statistics.txt", &paths->report
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    result = solar_project_resolve_path(
        project, ".solar/logs/yosys.stdout.log", &paths->stdout_log
    );
    if (result.status != SOLAR_STATUS_OK) return result;
    return solar_project_resolve_path(
        project, ".solar/logs/yosys.stderr.log", &paths->stderr_log
    );
}

static int write_quoted(FILE *script, const char *value)
{
    const char *cursor;

    if (fputc('"', script) == EOF) return -1;
    for (cursor = value; *cursor != '\0'; cursor++) {
        if ((*cursor == '\\' || *cursor == '"') && fputc('\\', script) == EOF) {
            return -1;
        }
        if (fputc((unsigned char)*cursor, script) == EOF) return -1;
    }
    return fputc('"', script) == EOF ? -1 : 0;
}

static bool system_verilog_path(const char *path)
{
    size_t length = strlen(path);

    return length > 3U && strcmp(path + length - 3U, ".sv") == 0;
}

static const char *path_file_name(const char *path)
{
    const char *separator = strrchr(path, '/');

    return separator == NULL ? path : separator + 1;
}

static int write_unquoted_file_name(FILE *script, const char *path)
{
    const unsigned char *character =
        (const unsigned char *)path_file_name(path);

    if (*character == '\0') return -1;
    while (*character != '\0') {
        if (isalnum(*character) == 0 && *character != '_' &&
            *character != '-' && *character != '.') {
            return -1;
        }
        if (fputc(*character, script) == EOF) return -1;
        character++;
    }
    return 0;
}

static int write_yosys_commands(
    FILE *script,
    const SolarProject *project,
    const YosysPaths *paths
)
{
    size_t index;

    for (index = 0U; index < project->config.sources.rtl.count; index++) {
        char *source_path = NULL;
        SolarResult result = solar_project_resolve_path(
            project, project->config.sources.rtl.items[index], &source_path
        );
        if (result.status != SOLAR_STATUS_OK) {
            free(source_path);
            return -1;
        }
        if (fputs(system_verilog_path(source_path)
                ? "read_verilog -sv " : "read_verilog ", script) == EOF ||
            write_quoted(script, source_path) != 0 || fputc('\n', script) == EOF) {
            free(source_path);
            return -1;
        }
        free(source_path);
    }
    if (fprintf(script, "hierarchy -check -top %s\n", project->config.synthesis.top) < 0 ||
        fputs("proc\nopt\ncheck\n", script) == EOF ||
        fputs("tee -o ", script) == EOF || write_quoted(script, paths->report) != 0 ||
        fprintf(script, " stat -top %s\n", project->config.synthesis.top) < 0 ||
        fputs("write_verilog -noattr ", script) == EOF ||
        write_quoted(script, paths->netlist) != 0 || fputc('\n', script) == EOF) {
        return -1;
    }
    return 0;
}

static SolarResult write_yosys_script(
    const SolarProject *project,
    const YosysPaths *paths
)
{
    int descriptor = open(
        paths->script,
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
        0644
    );
    FILE *script;

    if (descriptor < 0) {
        return yosys_error(
            SOLAR_STATUS_IO_ERROR,
            "ensure .solar/tmp/synth is a project-local directory",
            "cannot create Yosys script %s: %s",
            paths->script,
            strerror(errno)
        );
    }
    script = fdopen(descriptor, "w");
    if (script == NULL) {
        int saved_errno = errno;
        (void)close(descriptor);
        return yosys_error(
            SOLAR_STATUS_IO_ERROR,
            "check available file descriptors and try again",
            "cannot open Yosys script stream: %s",
            strerror(saved_errno)
        );
    }
    {
        int write_failed = write_yosys_commands(script, project, paths);
        int flush_failed = write_failed == 0 ? fflush(script) : 0;
        int saved_errno = (write_failed != 0 || flush_failed != 0) ? errno : 0;
        int close_failed = fclose(script);

        if (write_failed != 0 || flush_failed != 0 || close_failed != 0) {
            if (saved_errno == 0 && close_failed != 0) saved_errno = errno;
            if (saved_errno == 0) saved_errno = EIO;
            (void)unlink(paths->script);
            return yosys_error(
                SOLAR_STATUS_IO_ERROR,
                "check available storage and source paths, then try again",
                "cannot write Yosys script %s: %s",
                paths->script,
                strerror(saved_errno)
            );
        }
    }
    return solar_result_ok();
}

static SolarResult run_yosys(
    const SolarProject *project,
    const YosysPaths *paths
)
{
    const char *arguments[] = {"yosys", "-s", paths->script, NULL};
    SolarProcessSpec specification = {
        "yosys", arguments, project->root, paths->stdout_log, paths->stderr_log,
        NULL, 0U, NULL
    };
    SolarProcessResult process;
    SolarResult result;

    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    solar_process_result_free(&process);
    if (result.status != SOLAR_STATUS_OK) {
        solar_backend_add_log_context(&result, "yosys", paths->stderr_log);
        (void)snprintf(
            result.diagnostic.hint,
            sizeof(result.diagnostic.hint),
            "inspect Yosys stderr at %s",
            paths->stderr_log
        );
    }
    return result;
}

static SolarResult verify_yosys_artifacts(const YosysPaths *paths)
{
    struct stat information;

    if (stat(paths->netlist, &information) != 0 || !S_ISREG(information.st_mode)) {
        return yosys_error(
            SOLAR_STATUS_PROCESS_FAILED,
            "inspect the Yosys logs for write_verilog failures",
            "Yosys did not create netlist %s",
            paths->netlist
        );
    }
    if (stat(paths->report, &information) != 0 || !S_ISREG(information.st_mode)) {
        return yosys_error(
            SOLAR_STATUS_PROCESS_FAILED,
            "inspect the Yosys logs for stat failures",
            "Yosys did not create report %s",
            paths->report
        );
    }
    return solar_result_ok();
}

static SolarResult yosys_synthesize(const SolarProject *project)
{
    YosysPaths paths;
    SolarResult result = solar_filesystem_prepare_layout(project->root);

    if (result.status != SOLAR_STATUS_OK) {
        return result;
    }
    result = resolve_yosys_paths(project, &paths);
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_filesystem_reset_artifact(
            project->root, ".solar/tmp/synth/synthesis.ys"
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_filesystem_reset_artifact(
            project->root, ".solar/tmp/synth/netlist.v"
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_filesystem_reset_artifact(
            project->root, ".solar/tmp/synth/statistics.txt"
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = write_yosys_script(project, &paths);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = run_yosys(project, &paths);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = verify_yosys_artifacts(&paths);
    }
    yosys_paths_free(&paths);
    return result;
}

static int write_default_value(
    FILE *script,
    const char *option,
    const char *value
)
{
    if (fputc(' ', script) == EOF || fputs(option, script) == EOF) {
        return -1;
    }
    {
        const unsigned char *character = (const unsigned char *)value;

        while (*character != '\0') {
            bool allowed = isalnum(*character) != 0 ||
                *character == '_';

            if (strcmp(option, "-I") == 0) {
                allowed = allowed || *character == '-' ||
                    *character == '.' || *character == '/';
            } else {
                allowed = allowed || *character == '=';
            }
            if (!allowed) return -1;
            character++;
        }
    }
    if (fputs(value, script) == EOF) return -1;
    return 0;
}

static int write_request_yosys_commands(
    FILE *script,
    const SolarSynthesisRequest *request
)
{
    size_t source_index;
    size_t index;

    if (request->include_dirs.count > 0U || request->defines.count > 0U) {
        if (fputs("verilog_defaults -add", script) == EOF) return -1;
        for (index = 0U; index < request->include_dirs.count; index++) {
            if (write_default_value(
                    script, "-I", request->include_dirs.items[index]
                ) != 0) {
                return -1;
            }
        }
        for (index = 0U; index < request->defines.count; index++) {
            if (write_default_value(
                    script, "-D", request->defines.items[index]
                ) != 0) {
                return -1;
            }
        }
        if (fputc('\n', script) == EOF) return -1;
    }
    for (source_index = 0U;
         source_index < request->rtl_sources.count;
         source_index++) {
        if (fputs(system_verilog_path(request->rtl_sources.items[source_index])
                ? "read_verilog -sv " : "read_verilog ", script) == EOF ||
            write_quoted(script, request->rtl_sources.items[source_index]) != 0 ||
            fputc('\n', script) == EOF) {
            return -1;
        }
    }
    if (fprintf(script, "hierarchy -check -top %s\n", request->top) < 0 ||
        fputs("proc\nopt\ncheck\n", script) == EOF ||
        fputs("tee -o ", script) == EOF ||
        write_unquoted_file_name(script, request->report_path) != 0 ||
        fprintf(script, " stat -top %s\n", request->top) < 0 ||
        fputs("write_verilog -noattr ", script) == EOF ||
        write_unquoted_file_name(script, request->netlist_path) != 0 ||
        fputc('\n', script) == EOF) {
        return -1;
    }
    return 0;
}

static SolarResult write_request_yosys_script(
    const SolarSynthesisRequest *request
)
{
    int descriptor = open(
        request->script_path,
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
        0644
    );
    FILE *script;
    int write_failed;
    int flush_failed;
    int close_failed;
    int saved_errno;

    if (descriptor < 0) {
        return yosys_error(
            SOLAR_STATUS_IO_ERROR,
            "ensure .solar/tmp/synth is a project-local directory",
            "cannot create Yosys script %s: %s",
            request->script_path,
            strerror(errno)
        );
    }
    script = fdopen(descriptor, "w");
    if (script == NULL) {
        saved_errno = errno;
        (void)close(descriptor);
        return yosys_error(
            SOLAR_STATUS_IO_ERROR,
            "check available file descriptors and try again",
            "cannot open Yosys script stream: %s",
            strerror(saved_errno)
        );
    }
    write_failed = write_request_yosys_commands(script, request);
    flush_failed = write_failed == 0 ? fflush(script) : 0;
    saved_errno = (write_failed != 0 || flush_failed != 0) ? errno : 0;
    close_failed = fclose(script);
    if (write_failed != 0 || flush_failed != 0 || close_failed != 0) {
        if (saved_errno == 0 && close_failed != 0) saved_errno = errno;
        if (saved_errno == 0) saved_errno = EIO;
        (void)unlink(request->script_path);
        return yosys_error(
            SOLAR_STATUS_IO_ERROR,
            "check available storage and source paths, then try again",
            "cannot write Yosys script %s: %s",
            request->script_path,
            strerror(saved_errno)
        );
    }
    return solar_result_ok();
}

static SolarResult run_request_yosys(
    const SolarSynthesisRequest *request,
    uint64_t *duration_ns_out
)
{
    const char *arguments[] = {"yosys", "-s", request->script_path, NULL};
    SolarProcessSpec specification = {
        "yosys", arguments, request->working_directory,
        request->stdout_log, request->stderr_log, NULL, 0U, NULL
    };
    SolarProcessResult process;
    SolarResult result;

    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    *duration_ns_out = process.duration_ns;
    solar_process_result_free(&process);
    if (result.status != SOLAR_STATUS_OK) {
        solar_backend_add_log_context(&result, "yosys", request->stderr_log);
        (void)snprintf(
            result.diagnostic.hint,
            sizeof(result.diagnostic.hint),
            "inspect Yosys stderr at %s",
            request->stderr_log
        );
    }
    return result;
}

static SolarResult verify_request_yosys_artifacts(
    const SolarSynthesisRequest *request
)
{
    struct stat information;

    if (stat(request->netlist_path, &information) != 0 ||
        !S_ISREG(information.st_mode)) {
        return yosys_error(
            SOLAR_STATUS_PROCESS_FAILED,
            "inspect the Yosys logs for write_verilog failures",
            "Yosys did not create netlist %s",
            request->netlist_path
        );
    }
    if (stat(request->report_path, &information) != 0 ||
        !S_ISREG(information.st_mode)) {
        return yosys_error(
            SOLAR_STATUS_PROCESS_FAILED,
            "inspect the Yosys logs for stat failures",
            "Yosys did not create report %s",
            request->report_path
        );
    }
    return solar_result_ok();
}

static SolarResult yosys_synthesize_request(
    const SolarSynthesisRequest *request,
    uint64_t *duration_ns_out
)
{
    SolarResult result;

    if (request->project_root == NULL || request->working_directory == NULL ||
        request->top == NULL || request->rtl_sources.count == 0U ||
        request->script_path == NULL || request->netlist_path == NULL ||
        request->report_path == NULL || request->stdout_log == NULL ||
        request->stderr_log == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "Yosys received an incomplete synthesis request",
            "build the request through the Solar Synthesis Service"
        );
    }
    result = write_request_yosys_script(request);
    if (result.status == SOLAR_STATUS_OK) {
        result = run_request_yosys(request, duration_ns_out);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = verify_request_yosys_artifacts(request);
    }
    return result;
}

const SolarBackend SOLAR_YOSYS_BACKEND = {
    .name = "yosys",
    .capabilities = SOLAR_BACKEND_SYNTHESIS,
    .tools = YOSYS_TOOLS,
    .tool_count = sizeof(YOSYS_TOOLS) / sizeof(YOSYS_TOOLS[0]),
    .elaborate_tool_count = 0U,
    .elaborate_request = NULL,
    .simulate = NULL,
    .simulate_request = NULL,
    .synthesize = yosys_synthesize,
    .synthesize_request = yosys_synthesize_request
};

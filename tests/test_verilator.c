#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/backend.h"
#include "solar/filesystem.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *join_path(const char *left, const char *right)
{
    size_t length = strlen(left) + strlen(right) + 2U;
    char *path = malloc(length);

    if (path != NULL) (void)snprintf(path, length, "%s/%s", left, right);
    return path;
}

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "r");
    long length;
    char *text;

    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) return NULL;
    length = ftell(file);
    if (length < 0 || fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return NULL;
    }
    text = malloc((size_t)length + 1U);
    if (text == NULL || fread(text, 1U, (size_t)length, file) != (size_t)length) {
        free(text);
        (void)fclose(file);
        return NULL;
    }
    text[length] = '\0';
    if (fclose(file) != 0) {
        free(text);
        return NULL;
    }
    return text;
}

static int append(SolarStringList *list, const char *value)
{
    return solar_string_list_append_copy(list, value).status == SOLAR_STATUS_OK
        ? 0
        : -1;
}

int main(int argc, char **argv)
{
    char root_template[] = "/tmp/solar-verilator-XXXXXX";
    char *root = mkdtemp(root_template);
    char *tool = NULL;
    char *record = NULL;
    char *executable = NULL;
    char *waveform = NULL;
    char *compile_out = NULL;
    char *compile_err = NULL;
    char *run_out = NULL;
    char *run_err = NULL;
    char *spaced_working_directory = NULL;
    char *old_path = NULL;
    char *record_text = NULL;
    SolarSimulationRequest request;
    SolarRtlBuildRequest rtl_request;
    SolarSimulationFailureKind failure_kind;
    SolarSimulationMetrics metrics;
    uint64_t rtl_duration_ns = 0U;
    SolarResult result;
    int failed = 1;

    solar_simulation_request_init(&request);
    solar_rtl_build_request_init(&rtl_request);
    if (argc != 2 || root == NULL) goto cleanup;
    tool = join_path(root, "verilator");
    record = join_path(root, "record.log");
    executable = join_path(root, "simulation");
    waveform = join_path(root, "waves output.fst");
    compile_out = join_path(root, "compile.out");
    compile_err = join_path(root, "compile.err");
    run_out = join_path(root, "run.out");
    run_err = join_path(root, "run.err");
    old_path = getenv("PATH") == NULL ? NULL : strdup(getenv("PATH"));
    if (tool == NULL || record == NULL || executable == NULL || waveform == NULL ||
        compile_out == NULL || compile_err == NULL || run_out == NULL ||
        run_err == NULL || symlink(argv[1], tool) != 0 ||
        setenv("PATH", root, 1) != 0 ||
        setenv("SOLAR_BACKEND_RECORD", record, 1) != 0 ||
        setenv("SOLAR_BACKEND_WAVEFORM", "waves output.fst", 1) != 0) {
        goto cleanup;
    }
    request.backend = strdup("verilator");
    request.project_root = strdup(root);
    request.working_directory = strdup(root);
    request.test_name = strdup("verilator-test");
    request.top = strdup("design_tb");
    request.executable_path = strdup(executable);
    request.waveform_path = strdup(waveform);
    request.compile_stdout_log = strdup(compile_out);
    request.compile_stderr_log = strdup(compile_err);
    request.run_stdout_log = strdup(run_out);
    request.run_stderr_log = strdup(run_err);
    if (request.backend == NULL || request.project_root == NULL ||
        request.working_directory == NULL || request.test_name == NULL ||
        request.top == NULL || request.executable_path == NULL ||
        request.waveform_path == NULL || request.compile_stdout_log == NULL ||
        request.compile_stderr_log == NULL || request.run_stdout_log == NULL ||
        request.run_stderr_log == NULL ||
        append(&request.rtl_sources, "/rtl path/design.v") != 0 ||
        append(&request.test_sources, "/tb path/design_tb.v") != 0 ||
        append(&request.include_dirs, "/include path") != 0 ||
        append(&request.defines, "WIDTH=8") != 0) {
        goto cleanup;
    }
    result = solar_backend_simulate_request(
        &request, &failure_kind, &metrics
    );
    if (result.status == SOLAR_STATUS_OK) {
        if (setenv("SOLAR_BACKEND_NO_WAVEFORM", "1", 1) != 0) {
            result = solar_result_error(
                SOLAR_STATUS_INTERNAL_ERROR,
                "could not prepare no-waveform regression", ""
            );
        } else {
            SolarResult no_waveform_result = solar_backend_simulate_request(
                &request, &failure_kind, &metrics
            );

            (void)unsetenv("SOLAR_BACKEND_NO_WAVEFORM");
            if (no_waveform_result.status != SOLAR_STATUS_OK ||
                failure_kind != SOLAR_SIMULATION_FAILURE_NONE ||
                access(waveform, F_OK) == 0 || errno != ENOENT) {
                result = solar_result_error(
                    SOLAR_STATUS_INTERNAL_ERROR,
                    "Verilator treated a missing optional waveform as failure", ""
                );
            } else {
                result = solar_backend_simulate_request(
                    &request, &failure_kind, &metrics
                );
            }
        }
    }
    if (result.status == SOLAR_STATUS_OK) {
        char *normal_working_directory = request.working_directory;

        spaced_working_directory = join_path(root, "workspace with spaces");
        request.working_directory = spaced_working_directory == NULL
            ? NULL
            : strdup(spaced_working_directory);
        if (request.working_directory == NULL ||
            mkdir(spaced_working_directory, 0700) != 0) {
            result = solar_result_error(
                SOLAR_STATUS_INTERNAL_ERROR,
                "could not prepare whitespace-path regression", ""
            );
        } else {
            SolarResult whitespace_result = solar_backend_simulate_request(
                &request, &failure_kind, &metrics
            );

            if (whitespace_result.status != SOLAR_STATUS_CONFIG_ERROR ||
                strstr(
                    whitespace_result.diagnostic.message,
                    "project path containing whitespace"
                ) == NULL) {
                result = solar_result_error(
                    SOLAR_STATUS_INTERNAL_ERROR,
                    "Verilator accepted an unsupported whitespace workspace", ""
                );
            }
        }
        free(request.working_directory);
        request.working_directory = normal_working_directory;
    }
    if (result.status == SOLAR_STATUS_OK) {
        rtl_request.backend = strdup("verilator");
        rtl_request.project_root = strdup(root);
        rtl_request.working_directory = strdup(root);
        rtl_request.top = strdup("design");
        rtl_request.stdout_log = strdup(compile_out);
        rtl_request.stderr_log = strdup(compile_err);
        if (rtl_request.backend == NULL || rtl_request.project_root == NULL ||
            rtl_request.working_directory == NULL || rtl_request.top == NULL ||
            rtl_request.stdout_log == NULL || rtl_request.stderr_log == NULL ||
            append(&rtl_request.rtl_sources, "/rtl path/design.v") != 0 ||
            append(&rtl_request.include_dirs, "/include path") != 0 ||
            append(&rtl_request.defines, "WIDTH=8") != 0) {
            result = solar_result_error(
                SOLAR_STATUS_INTERNAL_ERROR,
                "could not build Verilator RTL test request", ""
            );
        } else {
            result = solar_backend_elaborate_request(
                &rtl_request, &rtl_duration_ns
            );
        }
    }
    record_text = read_file(record);
    if (result.status != SOLAR_STATUS_OK ||
        failure_kind != SOLAR_SIMULATION_FAILURE_NONE || record_text == NULL ||
        strstr(record_text, "verilator\t@cwd=") == NULL ||
        strstr(
            record_text,
            "\t--binary\t--timing\t--trace-fst\t--autoflush\t-Wno-fatal\t"
        ) == NULL ||
        strstr(record_text, "\t--top-module\tdesign_tb\t") == NULL ||
        strstr(record_text, "\t-I/include path\t-DWIDTH=8\t") == NULL ||
        strstr(record_text, "\t/rtl path/design.v\t/tb path/design_tb.v") == NULL ||
        strstr(record_text, "\t--lint-only\t--timing\t--top-module\tdesign\t") == NULL ||
        strstr(record_text, "--quiet-build") != NULL ||
        access(executable, X_OK) != 0 || access(waveform, R_OK) != 0) {
        (void)fprintf(
            stderr, "Verilator test failed: %s\nrecord:\n%s\n",
            result.diagnostic.message,
            record_text == NULL ? "(missing)" : record_text
        );
        goto cleanup;
    }
    failed = 0;

cleanup:
    if (old_path != NULL) (void)setenv("PATH", old_path, 1);
    else (void)unsetenv("PATH");
    (void)unsetenv("SOLAR_BACKEND_RECORD");
    (void)unsetenv("SOLAR_BACKEND_WAVEFORM");
    (void)unsetenv("SOLAR_BACKEND_NO_WAVEFORM");
    solar_simulation_request_free(&request);
    solar_rtl_build_request_free(&rtl_request);
    free(record_text);
    if (tool != NULL) (void)unlink(tool);
    if (record != NULL) (void)unlink(record);
    if (executable != NULL) (void)unlink(executable);
    if (waveform != NULL) (void)unlink(waveform);
    if (compile_out != NULL) (void)unlink(compile_out);
    if (compile_err != NULL) (void)unlink(compile_err);
    if (run_out != NULL) (void)unlink(run_out);
    if (run_err != NULL) (void)unlink(run_err);
    if (spaced_working_directory != NULL) {
        (void)rmdir(spaced_working_directory);
    }
    if (root != NULL) (void)rmdir(root);
    free(old_path);
    free(run_err);
    free(spaced_working_directory);
    free(run_out);
    free(compile_err);
    free(compile_out);
    free(waveform);
    free(executable);
    free(record);
    free(tool);
    return failed;
}

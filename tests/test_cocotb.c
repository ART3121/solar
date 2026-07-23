#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/cocotb.h"

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
    if (length < 0 || fseek(file, 0L, SEEK_SET) != 0) return NULL;
    text = malloc((size_t)length + 1U);
    if (text == NULL || fread(text, 1U, (size_t)length, file) != (size_t)length) {
        free(text);
        (void)fclose(file);
        return NULL;
    }
    text[length] = '\0';
    (void)fclose(file);
    return text;
}

int main(int argc, char **argv)
{
    char root_template[] = "/tmp/solar-cocotb-XXXXXX";
    char *root = mkdtemp(root_template);
    char *python = NULL;
    char *record = NULL;
    char *waveform = NULL;
    char *stdout_log = NULL;
    char *stderr_log = NULL;
    char *spaced_working_directory = NULL;
    char *old_path = NULL;
    char *record_text = NULL;
    SolarSimulationRequest request;
    SolarSimulationFailureKind failure_kind;
    SolarSimulationMetrics metrics;
    SolarResult result;
    int failed = 1;

    solar_simulation_request_init(&request);
    if (argc != 2 || root == NULL) goto cleanup;
    python = join_path(root, "python3");
    record = join_path(root, "record.log");
    waveform = join_path(root, "dump output.fst");
    stdout_log = join_path(root, "cocotb.out");
    stderr_log = join_path(root, "cocotb.err");
    old_path = getenv("PATH") == NULL ? NULL : strdup(getenv("PATH"));
    if (python == NULL || record == NULL || waveform == NULL ||
        stdout_log == NULL || stderr_log == NULL ||
        symlink(argv[1], python) != 0 || setenv("PATH", root, 1) != 0 ||
        setenv("SOLAR_BACKEND_RECORD", record, 1) != 0) goto cleanup;
    request.top = strdup("dut");
    request.working_directory = strdup(root);
    request.waveform_path = strdup(waveform);
    request.run_stdout_log = strdup(stdout_log);
    request.run_stderr_log = strdup(stderr_log);
    if (request.top == NULL || request.working_directory == NULL ||
        request.waveform_path == NULL || request.run_stdout_log == NULL ||
        request.run_stderr_log == NULL ||
        solar_string_list_append_copy(
            &request.rtl_sources, "/rtl path/dut.v"
        ).status != SOLAR_STATUS_OK ||
        solar_string_list_append_copy(
            &request.include_dirs, "/include path"
        ).status != SOLAR_STATUS_OK ||
        solar_string_list_append_copy(
            &request.defines, "WIDTH=8"
        ).status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_cocotb_run(
        &request, "/test path/test_dut.py", &failure_kind, &metrics
    );
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
                "could not prepare cocotb whitespace-path regression", ""
            );
        } else {
            SolarResult whitespace_result = solar_cocotb_run(
                &request, "/test path/test_dut.py", &failure_kind, &metrics
            );

            if (whitespace_result.status != SOLAR_STATUS_CONFIG_ERROR ||
                strstr(
                    whitespace_result.diagnostic.message,
                    "project path containing whitespace"
                ) == NULL) {
                result = solar_result_error(
                    SOLAR_STATUS_INTERNAL_ERROR,
                    "cocotb accepted an unsupported whitespace workspace", ""
                );
            }
        }
        free(request.working_directory);
        request.working_directory = normal_working_directory;
    }
    record_text = read_file(record);
    if (result.status != SOLAR_STATUS_OK ||
        failure_kind != SOLAR_SIMULATION_FAILURE_NONE ||
        access(waveform, R_OK) != 0 || record_text == NULL ||
        strstr(record_text, "python3\t@cwd=") == NULL ||
        strstr(record_text, "\t--top\tdut\t") == NULL ||
        strstr(record_text, "\t--test-file\t/test path/test_dut.py\t") == NULL ||
        strstr(record_text, "\t--source\t/rtl path/dut.v\t") == NULL ||
        strstr(record_text, "\t--include\t/include path\t") == NULL ||
        strstr(record_text, "\t--define\tWIDTH=8") == NULL) {
        (void)fprintf(
            stderr, "cocotb driver failed: %s\nrecord:\n%s\n",
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
    solar_simulation_request_free(&request);
    free(record_text);
    if (python != NULL) (void)unlink(python);
    if (record != NULL) (void)unlink(record);
    if (waveform != NULL) (void)unlink(waveform);
    if (stdout_log != NULL) (void)unlink(stdout_log);
    if (stderr_log != NULL) (void)unlink(stderr_log);
    if (spaced_working_directory != NULL) {
        (void)rmdir(spaced_working_directory);
    }
    if (root != NULL) (void)rmdir(root);
    free(old_path);
    free(stderr_log);
    free(spaced_working_directory);
    free(stdout_log);
    free(waveform);
    free(record);
    free(python);
    return failed;
}

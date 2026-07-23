#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/filesystem.h"
#include "solar/artifact.h"
#include "solar/project.h"
#include "solar/test.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char MANIFEST[] =
    "[solar]\n"
    "format = 2\n"
    "\n"
    "[project]\n"
    "name = \"test-service\"\n"
    "language = \"verilog\"\n"
    "default_test = \"basic\"\n"
    "default_profile = \"debug\"\n"
    "\n"
    "[sources]\n"
    "rtl = [\"rtl/design.v\"]\n"
    "include_dirs = [\"include/global space\"]\n"
    "defines = [\"DATA_WIDTH=8\", \"SHARED\"]\n"
    "\n"
    "[simulation]\n"
    "backend = \"iverilog\"\n"
    "\n"
    "[synthesis]\n"
    "backend = \"yosys\"\n"
    "top = \"design\"\n"
    "\n"
    "[[profile]]\n"
    "name = \"debug\"\n"
    "include_dirs = [\"profiles/debug include\"]\n"
    "defines = [\"DEBUG\", \"SHARED\"]\n"
    "\n"
    "[[profile]]\n"
    "name = \"release\"\n"
    "include_dirs = [\"profiles/release include\"]\n"
    "defines = [\"SYNTHESIS\"]\n"
    "\n"
    "[[test]]\n"
    "name = \"basic\"\n"
    "top = \"basic_tb\"\n"
    "sources = [\"tb/basic test.v\"]\n"
    "include_dirs = [\"tb/basic include\"]\n"
    "defines = [\"TEST_BASIC\", \"SHARED\"]\n"
    "waveform = \"waves/wave output.vcd\"\n"
    "\n"
    "[[test]]\n"
    "name = \"overflow\"\n"
    "top = \"overflow_tb\"\n"
    "sources = [\"tb/overflow test.v\"]\n"
    "defines = [\"TEST_OVERFLOW\"]\n"
    "waveform = \"waves/wave output.vcd\"\n"
    "\n"
    "[[test]]\n"
    "name = \"reset\"\n"
    "top = \"reset_tb\"\n"
    "sources = [\"tb/reset test.v\"]\n"
    "include_dirs = [\"tb/reset include\"]\n"
    "defines = [\"TEST_RESET\"]\n"
    "waveform = \"waves/wave output.vcd\"\n";

static int report_failure(const char *test_name, const char *message)
{
    (void)fprintf(stderr, "%s: %s\n", test_name, message);
    return 1;
}

static char *join_path(const char *left, const char *right)
{
    size_t capacity = strlen(left) + strlen(right) + 2U;
    char *path = malloc(capacity);

    if (path != NULL) (void)snprintf(path, capacity, "%s/%s", left, right);
    return path;
}

static int write_text_file(const char *path, const char *content)
{
    FILE *file = fopen(path, "w");
    int failed;

    if (file == NULL) return -1;
    failed = fputs(content, file) == EOF;
    if (fclose(file) != 0) failed = 1;
    return failed ? -1 : 0;
}

static char *read_text_file(const char *path)
{
    FILE *file = fopen(path, "r");
    long length;
    char *contents;

    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) (void)fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length < 0L || fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return NULL;
    }
    contents = malloc((size_t)length + 1U);
    if (contents == NULL) {
        (void)fclose(file);
        return NULL;
    }
    if (fread(contents, 1U, (size_t)length, file) != (size_t)length) {
        free(contents);
        (void)fclose(file);
        return NULL;
    }
    contents[length] = '\0';
    if (fclose(file) != 0) {
        free(contents);
        return NULL;
    }
    return contents;
}

static bool regular_file_exists(const char *path)
{
    struct stat information;

    return stat(path, &information) == 0 && S_ISREG(information.st_mode);
}

static int create_directory(const char *root, const char *relative)
{
    char *path = join_path(root, relative);
    int status = path == NULL ? -1 : mkdir(path, 0700);

    free(path);
    return status;
}

static int create_file(
    const char *root,
    const char *relative,
    const char *content
)
{
    char *path = join_path(root, relative);
    int status = path == NULL ? -1 : write_text_file(path, content);

    free(path);
    return status;
}

static int create_project_files(const char *root)
{
    const char *directories[] = {
        "rtl",
        "tb",
        "include",
        "include/global space",
        "profiles",
        "profiles/debug include",
        "profiles/release include",
        "tb/basic include",
        "tb/reset include"
    };
    const char *files[] = {
        "rtl/design.v",
        "tb/basic test.v",
        "tb/overflow test.v",
        "tb/reset test.v"
    };
    size_t index;

    for (index = 0U; index < sizeof(directories) / sizeof(directories[0]); index++) {
        if (create_directory(root, directories[index]) != 0) return -1;
    }
    if (create_file(root, "solar.toml", MANIFEST) != 0) return -1;
    for (index = 0U; index < sizeof(files) / sizeof(files[0]); index++) {
        if (create_file(root, files[index], "module placeholder; endmodule\n") != 0) {
            return -1;
        }
    }
    return 0;
}

static void remove_project_files(const char *root)
{
    const char *files[] = {
        "solar.toml",
        "rtl/design.v",
        "tb/basic test.v",
        "tb/overflow test.v",
        "tb/reset test.v"
    };
    const char *directories[] = {
        "tb/reset include",
        "tb/basic include",
        "profiles/release include",
        "profiles/debug include",
        "profiles",
        "include/global space",
        "include",
        "tb",
        "rtl"
    };
    size_t index;
    bool removed = false;

    if (root == NULL) return;
    (void)solar_filesystem_clean_project(root, &removed);
    for (index = 0U; index < sizeof(files) / sizeof(files[0]); index++) {
        char *path = join_path(root, files[index]);

        if (path != NULL) (void)unlink(path);
        free(path);
    }
    for (index = 0U; index < sizeof(directories) / sizeof(directories[0]); index++) {
        char *path = join_path(root, directories[index]);

        if (path != NULL) (void)rmdir(path);
        free(path);
    }
    (void)rmdir(root);
}

static int create_tool_links(const char *directory, const char *helper_path)
{
    const char *tools[] = {"iverilog", "vvp"};
    size_t index;

    for (index = 0U; index < sizeof(tools) / sizeof(tools[0]); index++) {
        char *path = join_path(directory, tools[index]);
        int status = path == NULL ? -1 : symlink(helper_path, path);

        free(path);
        if (status != 0) return -1;
    }
    return 0;
}

static void remove_tool_links(const char *directory)
{
    const char *tools[] = {"iverilog", "vvp"};
    size_t index;

    if (directory == NULL) return;
    for (index = 0U; index < sizeof(tools) / sizeof(tools[0]); index++) {
        char *path = join_path(directory, tools[index]);

        if (path != NULL) (void)unlink(path);
        free(path);
    }
    (void)rmdir(directory);
}

static bool path_has_suffix(const char *path, const char *suffix)
{
    size_t path_length;
    size_t suffix_length;

    if (path == NULL || suffix == NULL) return false;
    path_length = strlen(path);
    suffix_length = strlen(suffix);
    return path_length >= suffix_length &&
        strcmp(path + path_length - suffix_length, suffix) == 0;
}

static bool record_has_sequence(
    const char *record,
    const char *tool,
    const char *first,
    const char *second
)
{
    size_t capacity = strlen(first) + strlen(second) + 3U;
    char *needle = malloc(capacity);
    const char *line = record;
    bool found = false;

    if (needle == NULL) return false;
    (void)snprintf(needle, capacity, "\t%s\t%s", first, second);
    while (line != NULL && *line != '\0') {
        const char *line_end = strchr(line, '\n');
        const char *match;

        if (line_end == NULL) line_end = line + strlen(line);
        if (strncmp(line, tool, strlen(tool)) == 0 && line[strlen(tool)] == '\t') {
            match = strstr(line, needle);
            if (match != NULL && match < line_end) {
                found = true;
                break;
            }
        }
        line = *line_end == '\0' ? NULL : line_end + 1;
    }
    free(needle);
    return found;
}

static bool record_has_token(
    const char *record,
    const char *tool,
    const char *token
)
{
    size_t capacity = strlen(token) + 3U;
    char *needle = malloc(capacity);
    const char *line = record;
    bool found = false;

    if (needle == NULL) return false;
    (void)snprintf(needle, capacity, "\t%s", token);
    while (line != NULL && *line != '\0') {
        const char *line_end = strchr(line, '\n');
        const char *match;

        if (line_end == NULL) line_end = line + strlen(line);
        if (strncmp(line, tool, strlen(tool)) == 0 && line[strlen(tool)] == '\t') {
            match = strstr(line, needle);
            if (match != NULL && match < line_end &&
                (match + strlen(needle) == line_end ||
                 match[strlen(needle)] == '\t')) {
                found = true;
                break;
            }
        }
        line = *line_end == '\0' ? NULL : line_end + 1;
    }
    free(needle);
    return found;
}

static bool record_has_cwd(
    const char *record,
    const char *tool,
    const char *directory
)
{
    size_t capacity = strlen(directory) + sizeof("@cwd=");
    char *token = malloc(capacity);
    bool found;

    if (token == NULL) return false;
    (void)snprintf(token, capacity, "@cwd=%s", directory);
    found = record_has_token(record, tool, token);
    free(token);
    return found;
}

static char *artifact_path(
    const char *root,
    const char *test_name,
    const char *file_name
)
{
    const char *base = strrchr(file_name, '/');
    size_t capacity;
    char *path;

    (void)test_name;
    base = base == NULL ? file_name : base + 1;
    capacity = strlen(root) + strlen(base) + sizeof("/sim/");
    path = malloc(capacity);

    if (path != NULL) {
        (void)snprintf(
            path, capacity, "%s/sim/%s", root, base
        );
    }
    return path;
}

static char *test_directory_path(const char *root, const char *test_name)
{
    size_t capacity = strlen(root) + strlen(test_name) +
        sizeof("/.solar/tmp/tests/");
    char *path = malloc(capacity);

    if (path != NULL) {
        (void)snprintf(path, capacity, "%s/.solar/tmp/tests/%s", root, test_name);
    }
    return path;
}

static int verify_default_request(
    const char *root,
    const char *record_path,
    const SolarTestResult *test_result
)
{
    const char *test_name = "default test request";
    char *record = read_text_file(record_path);
    char *global_include = join_path(root, "include/global space");
    char *profile_include = join_path(root, "profiles/debug include");
    char *test_include = join_path(root, "tb/basic include");
    char *test_source = join_path(root, "tb/basic test.v");
    char *working_directory = test_directory_path(root, "basic");
    char *waveform = artifact_path(
        root, "basic", "waves/wave output.vcd"
    );
    int failed = 0;

    if (record == NULL || global_include == NULL || profile_include == NULL ||
        test_include == NULL || test_source == NULL || working_directory == NULL ||
        waveform == NULL) {
        failed = report_failure(test_name, "could not inspect fake backend output");
        goto cleanup;
    }
    if (test_result->result.status != SOLAR_STATUS_OK ||
        strcmp(test_result->name, "basic") != 0 ||
        strcmp(test_result->profile_name, "debug") != 0 ||
        !path_has_suffix(
            test_result->executable_path,
            "/.solar/tmp/tests/basic/simulation.vvp"
        ) ||
        strcmp(test_result->waveform_path, waveform) != 0 ||
        test_result->output == NULL ||
        strstr(test_result->output, "display from basic") == NULL ||
        !regular_file_exists(test_result->executable_path) ||
        !regular_file_exists(test_result->waveform_path)) {
        failed = report_failure(test_name, "result metadata or isolated artifacts are wrong");
        goto cleanup;
    }
    if (!record_has_sequence(record, "iverilog", "-s", "basic_tb") ||
        !record_has_sequence(record, "iverilog", "-I", global_include) ||
        !record_has_sequence(record, "iverilog", "-I", profile_include) ||
        !record_has_sequence(record, "iverilog", "-I", test_include) ||
        !record_has_token(record, "iverilog", "-DDATA_WIDTH=8") ||
        !record_has_token(record, "iverilog", "-DDEBUG") ||
        !record_has_token(record, "iverilog", "-DSHARED") ||
        !record_has_token(record, "iverilog", "-DTEST_BASIC") ||
        !record_has_token(record, "iverilog", test_source)) {
        failed = report_failure(
            test_name,
            "Icarus argv did not preserve top, separate includes/defines, or sources"
        );
    } else if (!record_has_cwd(record, "iverilog", working_directory) ||
               !record_has_cwd(record, "vvp", working_directory)) {
        failed = report_failure(
            test_name,
            "Icarus compile/run did not use the isolated test working directory"
        );
    }

cleanup:
    free(waveform);
    free(working_directory);
    free(test_source);
    free(test_include);
    free(profile_include);
    free(global_include);
    free(record);
    return failed;
}

static int verify_named_profile_request(
    const char *root,
    const char *record_path,
    const SolarTestResult *test_result
)
{
    const char *test_name = "named test and profile request";
    char *record = read_text_file(record_path);
    char *release_include = join_path(root, "profiles/release include");
    char *debug_include = join_path(root, "profiles/debug include");
    int failed = 0;

    if (record == NULL || release_include == NULL || debug_include == NULL) {
        failed = report_failure(test_name, "could not inspect named request");
        goto cleanup;
    }
    if (test_result->result.status != SOLAR_STATUS_OK ||
        strcmp(test_result->name, "reset") != 0 ||
        strcmp(test_result->profile_name, "release") != 0 ||
        !path_has_suffix(
            test_result->waveform_path,
            "/sim/wave output.vcd"
        ) ||
        !record_has_sequence(record, "iverilog", "-s", "reset_tb") ||
        !record_has_sequence(record, "iverilog", "-I", release_include) ||
        record_has_sequence(record, "iverilog", "-I", debug_include) ||
        !record_has_token(record, "iverilog", "-DSYNTHESIS") ||
        record_has_token(record, "iverilog", "-DDEBUG") ||
        !record_has_token(record, "iverilog", "-DTEST_RESET")) {
        failed = report_failure(test_name, "explicit test/profile selection was ignored");
    }

cleanup:
    free(debug_include);
    free(release_include);
    free(record);
    return failed;
}

static const char *find_vvp_cwd(const char *record, const char *directory)
{
    size_t capacity = strlen(directory) + sizeof("vvp\t@cwd=");
    char *needle = malloc(capacity);
    const char *match;

    if (needle == NULL) return NULL;
    (void)snprintf(needle, capacity, "vvp\t@cwd=%s", directory);
    match = strstr(record, needle);
    free(needle);
    return match;
}

static int verify_run_all(
    const char *root,
    const char *record_path,
    const SolarTestSummary *summary
)
{
    const char *test_name = "run all continues after failure";
    char *record = read_text_file(record_path);
    char *basic_directory = test_directory_path(root, "basic");
    char *overflow_directory = test_directory_path(root, "overflow");
    char *reset_directory = test_directory_path(root, "reset");
    char *basic_waveform = artifact_path(
        root, "basic", "waves/wave output.vcd"
    );
    char *reset_waveform = artifact_path(
        root, "reset", "waves/wave output.vcd"
    );
    const char *basic_run;
    const char *overflow_run;
    const char *reset_run;
    int failed = 0;

    if (record == NULL || basic_directory == NULL || overflow_directory == NULL ||
        reset_directory == NULL || basic_waveform == NULL || reset_waveform == NULL) {
        failed = report_failure(test_name, "could not inspect run-all output");
        goto cleanup;
    }
    basic_run = find_vvp_cwd(record, basic_directory);
    overflow_run = find_vvp_cwd(record, overflow_directory);
    reset_run = find_vvp_cwd(record, reset_directory);
    if (summary->count != 3U || summary->passed != 2U || summary->failed != 1U ||
        strcmp(summary->results[0].name, "basic") != 0 ||
        summary->results[0].result.status != SOLAR_STATUS_OK ||
        strcmp(summary->results[1].name, "overflow") != 0 ||
        summary->results[1].result.status != SOLAR_STATUS_PROCESS_FAILED ||
        summary->results[1].failure_kind != SOLAR_TEST_FAILURE_LOGICAL ||
        strcmp(summary->results[2].name, "reset") != 0 ||
        summary->results[2].result.status != SOLAR_STATUS_OK ||
        basic_run == NULL || overflow_run == NULL || reset_run == NULL ||
        !(basic_run < overflow_run && overflow_run < reset_run) ||
        !regular_file_exists(basic_waveform) ||
        !regular_file_exists(reset_waveform)) {
        failed = report_failure(
            test_name,
            "summary, ordering, continuation, isolation, or waveforms are incorrect"
        );
    }

cleanup:
    free(reset_waveform);
    free(basic_waveform);
    free(reset_directory);
    free(overflow_directory);
    free(basic_directory);
    free(record);
    return failed;
}

static int verify_success_without_waveform(
    const char *root,
    const SolarTestResult *test_result
)
{
    const char *test_name = "successful simulation without waveform";
    SolarArtifactList artifacts;
    SolarResult result;
    int failed = 0;

    solar_artifact_list_init(&artifacts);
    result = solar_artifact_list_load(root, &artifacts);
    if (test_result->result.status != SOLAR_STATUS_OK ||
        test_result->failure_kind != SOLAR_TEST_FAILURE_NONE ||
        test_result->waveform_path != NULL || test_result->output == NULL ||
        strstr(test_result->output, "display from overflow") == NULL ||
        result.status != SOLAR_STATUS_OK ||
        solar_artifact_find(&artifacts, "waveform", "overflow") != NULL) {
        failed = report_failure(
            test_name,
            "a missing optional waveform changed the result or was registered"
        );
    }
    solar_artifact_list_free(&artifacts);
    return failed;
}

static int verify_large_output_is_truncated(const SolarTestResult *test_result)
{
    const char *notice = "[solar: simulation output truncated at 16 MiB]";

    if (test_result->result.status != SOLAR_STATUS_OK ||
        test_result->output == NULL || strstr(test_result->output, notice) == NULL ||
        strlen(test_result->output) > (size_t)16U * 1024U * 1024U + 128U) {
        return report_failure(
            "large simulation output", "output was not bounded predictably"
        );
    }
    return 0;
}

static int clear_record(const char *record_path)
{
    if (unlink(record_path) == 0 || errno == ENOENT) return 0;
    return -1;
}

int main(int argc, char **argv)
{
    char project_template[] = "/tmp/solar test-service-XXXXXX";
    char tools_template[] = "/tmp/solar-test-tools-XXXXXX";
    char *project_root = NULL;
    char *tool_directory = NULL;
    char *record_path = NULL;
    char *old_path = NULL;
    SolarProject project;
    SolarTestResult test_result;
    SolarTestSummary summary;
    SolarResult result;
    int failures = 0;

    solar_project_init(&project);
    solar_test_result_init(&test_result);
    solar_test_summary_init(&summary);
    if (argc != 2) return report_failure("test service", "expected helper path");
    project_root = mkdtemp(project_template);
    tool_directory = mkdtemp(tools_template);
    if (project_root == NULL || tool_directory == NULL) {
        failures = report_failure("test service", "mkdtemp failed");
        goto cleanup;
    }
    record_path = join_path(project_root, "backend-record.tsv");
    old_path = getenv("PATH") == NULL ? NULL : strdup(getenv("PATH"));
    if (record_path == NULL || create_project_files(project_root) != 0 ||
        create_tool_links(tool_directory, argv[1]) != 0 ||
        setenv("PATH", tool_directory, 1) != 0 ||
        setenv("SOLAR_BACKEND_RECORD", record_path, 1) != 0 ||
        setenv("SOLAR_BACKEND_WAVEFORM", "waves/wave output.vcd", 1) != 0) {
        failures = report_failure("test service", "could not prepare fixture");
        goto cleanup;
    }
    result = solar_project_load(project_root, &project);
    if (result.status == SOLAR_STATUS_OK) result = solar_project_validate(&project);
    if (result.status != SOLAR_STATUS_OK) {
        failures = report_failure("test service", result.diagnostic.message);
        goto cleanup;
    }

    result = solar_test_run(&project, NULL, NULL, &test_result);
    if (result.status != SOLAR_STATUS_OK) {
        failures += report_failure("default test request", result.diagnostic.message);
    } else {
        failures += verify_default_request(project_root, record_path, &test_result);
    }
    solar_test_result_free(&test_result);

    if (clear_record(record_path) != 0) {
        failures += report_failure("named test request", "could not clear record");
        goto cleanup;
    }
    result = solar_test_run(&project, "reset", "release", &test_result);
    if (result.status != SOLAR_STATUS_OK) {
        failures += report_failure("named test request", result.diagnostic.message);
    } else {
        failures += verify_named_profile_request(project_root, record_path, &test_result);
    }
    solar_test_result_free(&test_result);

    if (clear_record(record_path) != 0 ||
        setenv("SOLAR_BACKEND_NO_WAVEFORM", "1", 1) != 0) {
        failures += report_failure(
            "successful simulation without waveform",
            "could not prepare missing-waveform injection"
        );
        goto cleanup;
    }
    result = solar_test_run(&project, "overflow", NULL, &test_result);
    if (result.status != SOLAR_STATUS_OK) {
        failures += report_failure(
            "successful simulation without waveform", result.diagnostic.message
        );
    } else {
        failures += verify_success_without_waveform(project_root, &test_result);
    }
    solar_test_result_free(&test_result);
    (void)unsetenv("SOLAR_BACKEND_NO_WAVEFORM");

    if (clear_record(record_path) != 0 ||
        setenv("SOLAR_BACKEND_LARGE_OUTPUT", "1", 1) != 0) {
        failures += report_failure(
            "large simulation output", "could not prepare large output injection"
        );
        goto cleanup;
    }
    result = solar_test_run(&project, "basic", NULL, &test_result);
    if (result.status != SOLAR_STATUS_OK) {
        failures += report_failure("large simulation output", result.diagnostic.message);
    } else {
        failures += verify_large_output_is_truncated(&test_result);
    }
    solar_test_result_free(&test_result);
    (void)unsetenv("SOLAR_BACKEND_LARGE_OUTPUT");

    if (clear_record(record_path) != 0 ||
        setenv("SOLAR_BACKEND_FAIL_TEST", "overflow", 1) != 0) {
        failures += report_failure("run all", "could not prepare failure injection");
        goto cleanup;
    }
    result = solar_test_run_all(&project, NULL, &summary);
    if (result.status != SOLAR_STATUS_OK) {
        failures += report_failure("run all", result.diagnostic.message);
    } else {
        failures += verify_run_all(project_root, record_path, &summary);
    }

cleanup:
    if (old_path != NULL) (void)setenv("PATH", old_path, 1);
    else (void)unsetenv("PATH");
    (void)unsetenv("SOLAR_BACKEND_RECORD");
    (void)unsetenv("SOLAR_BACKEND_WAVEFORM");
    (void)unsetenv("SOLAR_BACKEND_NO_WAVEFORM");
    (void)unsetenv("SOLAR_BACKEND_FAIL_TEST");
    (void)unsetenv("SOLAR_BACKEND_LARGE_OUTPUT");
    solar_test_summary_free(&summary);
    solar_test_result_free(&test_result);
    solar_project_free(&project);
    if (record_path != NULL) (void)unlink(record_path);
    remove_project_files(project_root);
    remove_tool_links(tool_directory);
    free(old_path);
    free(record_path);
    return failures == 0 ? 0 : 1;
}

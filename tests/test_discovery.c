#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/filesystem.h"
#include "solar/project.h"
#include "solar/runner.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *join_path(const char *directory, const char *relative)
{
    size_t length = strlen(directory) + strlen(relative) + 2U;
    char *path = malloc(length);

    if (path != NULL) (void)snprintf(path, length, "%s/%s", directory, relative);
    return path;
}

static int write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");

    if (file == NULL) return -1;
    if (fputs(text, file) == EOF) {
        (void)fclose(file);
        return -1;
    }
    return fclose(file);
}

static int failure(const char *message)
{
    (void)fprintf(stderr, "discovery: %s\n", message);
    return 1;
}

static bool contains(const SolarStringList *list, const char *value)
{
    size_t index;

    for (index = 0U; index < list->count; index++) {
        if (strcmp(list->items[index], value) == 0) return true;
    }
    return false;
}

static void cleanup_fixture(const char *root)
{
    const char *files[] = {
        "rtl/link.v", "rtl/z.v", "rtl/sub/a.v", "tb/helper.v",
        "tb/smoke.v", "solar.toml"
    };
    const char *directories[] = {"rtl/sub", "rtl", "tb"};
    bool removed = false;
    size_t index;

    (void)solar_filesystem_clean_project(root, &removed);
    for (index = 0U; index < sizeof(files) / sizeof(files[0]); index++) {
        char *path = join_path(root, files[index]);
        if (path != NULL) (void)unlink(path);
        free(path);
    }
    for (index = 0U;
         index < sizeof(directories) / sizeof(directories[0]);
         index++) {
        char *path = join_path(root, directories[index]);
        if (path != NULL) (void)rmdir(path);
        free(path);
    }
    (void)rmdir(root);
}

static int create_fixture(const char *root)
{
    static const char manifest[] =
        "[solar]\n"
        "format = 2\n\n"
        "[project]\n"
        "name = \"real-design\"\n"
        "language = \"verilog\"\n\n"
        "[simulation]\n"
        "backend = \"iverilog\"\n\n"
        "[synthesis]\n"
        "backend = \"yosys\"\n";
    static const char testbench[] =
        "// module ignored_comment;\n"
        "// $dumpfile(\"ignored.vcd\");\n"
        "module smoke_helper; endmodule\n"
        "module smoke_top;\n"
        "  smoke_helper helper();\n"
        "  initial begin\n"
        "    $dumpfile(\"waves/smoke.vcd\");\n"
        "    $finish;\n"
        "  end\n"
        "endmodule\n";
    char *rtl = join_path(root, "rtl");
    char *nested = join_path(root, "rtl/sub");
    char *tb = join_path(root, "tb");
    char *manifest_path = join_path(root, "solar.toml");
    char *a = join_path(root, "rtl/sub/a.v");
    char *z = join_path(root, "rtl/z.v");
    char *helper = join_path(root, "tb/helper.v");
    char *smoke = join_path(root, "tb/smoke.v");
    char *link = join_path(root, "rtl/link.v");
    int result = -1;

    if (rtl == NULL || nested == NULL || tb == NULL || manifest_path == NULL ||
        a == NULL || z == NULL || helper == NULL || smoke == NULL || link == NULL) {
        goto cleanup;
    }
    if (mkdir(rtl, 0700) != 0 || mkdir(nested, 0700) != 0 ||
        mkdir(tb, 0700) != 0 || write_text(manifest_path, manifest) != 0 ||
        write_text(a, "module leaf #(parameter WIDTH = 1); endmodule\n") != 0 ||
        write_text(z,
            "module chip; leaf #(.WIDTH(8)) child(); endmodule\n") != 0 ||
        write_text(helper, "`timescale 1ns/1ps\n") != 0 ||
        write_text(smoke, testbench) != 0 || symlink("/etc/passwd", link) != 0) {
        goto cleanup;
    }
    result = 0;

cleanup:
    free(link);
    free(smoke);
    free(helper);
    free(z);
    free(a);
    free(manifest_path);
    free(tb);
    free(nested);
    free(rtl);
    return result;
}

static int verify_core_discovery(const char *root)
{
    SolarProject project;
    SolarResult result;
    const SolarTest *test;
    int failed = 0;

    solar_project_init(&project);
    result = solar_project_load(root, &project);
    if (result.status == SOLAR_STATUS_OK) result = solar_project_validate(&project);
    if (result.status != SOLAR_STATUS_OK) {
        failed = failure(result.diagnostic.message);
        goto cleanup;
    }
    test = solar_config_find_test(&project.config, "smoke");
    if (project.discovery.rtl_files_found != 2U ||
        project.discovery.testbench_files_found != 2U ||
        project.discovery.rtl_files_added != 2U ||
        project.discovery.tests_added != 1U ||
        !project.discovery.synthesis_top_inferred ||
        project.config.sources.rtl.count != 2U ||
        strcmp(project.config.sources.rtl.items[0], "rtl/sub/a.v") != 0 ||
        strcmp(project.config.sources.rtl.items[1], "rtl/z.v") != 0 ||
        project.config.synthesis.top == NULL ||
        strcmp(project.config.synthesis.top, "chip") != 0 ||
        test == NULL || strcmp(test->top, "smoke_top") != 0 ||
        strcmp(test->waveform, "waves/smoke.vcd") != 0 ||
        !contains(&test->sources, "tb/helper.v") ||
        !contains(&test->sources, "tb/smoke.v")) {
        failed = failure("the in-memory inventory was not merged deterministically");
    }

cleanup:
    solar_project_free(&project);
    return failed;
}

static int verify_scan_command(const char *root, const char *solar_path)
{
    const char *arguments[] = {solar_path, "scan", NULL};
    SolarProcessSpec specification = {
        solar_path, arguments, root, NULL, NULL, NULL, 0U, NULL
    };
    SolarProcessResult process;
    SolarResult result;
    int failed = 0;

    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    if (result.status != SOLAR_STATUS_OK || process.exit_code != 0 ||
        process.stdout_text == NULL ||
        strstr(process.stdout_text, "RTL:   2 total") == NULL ||
        strstr(process.stdout_text, "Tests: 1 total") == NULL ||
        strstr(process.stdout_text, "Manifest: updated") == NULL) {
        failed = failure("solar scan did not synchronize the discovered inventory");
        if (process.stdout_text != NULL) (void)fprintf(stderr, "%s", process.stdout_text);
        if (process.stderr_text != NULL) (void)fprintf(stderr, "%s", process.stderr_text);
    }
    solar_process_result_free(&process);
    return failed;
}

static int verify_oversized_source_is_rejected(const char *root)
{
    char *path = join_path(root, "rtl/oversized.v");
    SolarProject project;
    SolarResult result;
    int descriptor = -1;
    int failed = 0;

    solar_project_init(&project);
    if (path == NULL) return failure("could not allocate oversized source path");
    descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor < 0 || ftruncate(descriptor, (off_t)65 * 1024 * 1024) != 0) {
        failed = failure("could not prepare oversized source");
        goto cleanup;
    }
    (void)close(descriptor);
    descriptor = -1;
    result = solar_project_load(root, &project);
    if (result.status != SOLAR_STATUS_CONFIG_ERROR ||
        strstr(result.diagnostic.message, "64 MiB") == NULL) {
        failed = failure("oversized discovered source was accepted");
    }

cleanup:
    if (descriptor >= 0) (void)close(descriptor);
    (void)unlink(path);
    solar_project_free(&project);
    free(path);
    return failed;
}

int main(int argc, char **argv)
{
    char root_template[] = "/tmp/solar-discovery-XXXXXX";
    char *root;
    int failures = 0;

    if (argc != 2) return failure("expected the solar executable path");
    root = mkdtemp(root_template);
    if (root == NULL) return failure("mkdtemp failed");
    if (create_fixture(root) != 0) {
        cleanup_fixture(root);
        return failure("could not create the temporary fixture");
    }
    failures += verify_core_discovery(root);
    failures += verify_scan_command(root, argv[1]);
    failures += verify_oversized_source_is_rejected(root);
    cleanup_fixture(root);
    return failures == 0 ? 0 : 1;
}

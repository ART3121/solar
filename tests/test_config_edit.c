#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/config_edit.h"
#include "solar/filesystem.h"
#include "solar/project.h"

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

static int write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    int failed;

    if (file == NULL) return -1;
    failed = fputs(text, file) == EOF;
    if (fclose(file) != 0) failed = 1;
    return failed ? -1 : 0;
}

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "r");
    char *text;
    long length;

    if (file == NULL || fseek(file, 0L, SEEK_END) != 0 ||
        (length = ftell(file)) < 0L || fseek(file, 0L, SEEK_SET) != 0) {
        if (file != NULL) (void)fclose(file);
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

static int failure(const char *message)
{
    (void)fprintf(stderr, "config edit: %s\n", message);
    return 1;
}

static void cleanup(const char *root)
{
    const char *files[] = {
        "solar.toml", "rtl/design.v", "tb/basic.v", "tb/overflow.v"
    };
    const char *directories[] = {"rtl", "tb"};
    bool removed = false;
    size_t index;

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

static int prepare_project(const char *root, char **manifest_out)
{
    static const char manifest_text[] =
        "# keep this project comment\n"
        "[solar]\nformat = 2\n\n"
        "[project]\n"
        "name = \"original\"\n"
        "language = \"verilog\"\n"
        "default_test = \"missing\"\n\n"
        "[sources]\nrtl = [\"rtl/design.v\"]\n\n"
        "[simulation]\nbackend = \"iverilog\"\n\n"
        "[synthesis]\nbackend = \"yosys\"\ntop = \"design\"\n\n"
        "[[profile]]\nname = \"paper\"\ndefines = [\"PAPER\"]\n\n"
        "[[test]]\nname = \"basic\"\ntop = \"basic_tb\"\n"
        "sources = [\"tb/basic.v\"]\nwaveform = \"basic.vcd\"\n\n"
        "[[test]]\nname = \"overflow\"\ntop = \"overflow_tb\"\n"
        "sources = [\"tb/overflow.v\"]\nwaveform = \"overflow.vcd\"\n";
    char *rtl_directory = join_path(root, "rtl");
    char *tb_directory = join_path(root, "tb");
    char *manifest = join_path(root, "solar.toml");
    char *rtl = join_path(root, "rtl/design.v");
    char *basic = join_path(root, "tb/basic.v");
    char *overflow = join_path(root, "tb/overflow.v");
    int failed = rtl_directory == NULL || tb_directory == NULL ||
        manifest == NULL || rtl == NULL || basic == NULL || overflow == NULL ||
        mkdir(rtl_directory, 0700) != 0 || mkdir(tb_directory, 0700) != 0 ||
        write_file(manifest, manifest_text) != 0 ||
        write_file(rtl, "module design; endmodule\n") != 0 ||
        write_file(basic, "module basic_tb; endmodule\n") != 0 ||
        write_file(overflow, "module overflow_tb; endmodule\n") != 0;

    free(overflow);
    free(basic);
    free(rtl);
    free(tb_directory);
    free(rtl_directory);
    if (failed) {
        free(manifest);
        return -1;
    }
    *manifest_out = manifest;
    return 0;
}

int main(void)
{
    char root_template[] = "/tmp/solar-config-edit-XXXXXX";
    char *root = mkdtemp(root_template);
    char *manifest = NULL;
    char *updated = NULL;
    char *unchanged = NULL;
    char *after_failure = NULL;
    char *legacy_after = NULL;
    SolarConfigEditRequest request = {
        "Benchmark \"A\"", "renamed_top", "overflow"
    };
    SolarProject project;
    SolarResult result;
    bool changed = false;
    int failed = 0;

    solar_project_init(&project);
    if (root == NULL || prepare_project(root, &manifest) != 0) {
        cleanup(root);
        free(manifest);
        return failure("could not prepare fixture");
    }
    result = solar_config_edit_project(root, &request, &changed);
    updated = read_file(manifest);
    if (result.status != SOLAR_STATUS_OK || !changed || updated == NULL ||
        strstr(updated, "# keep this project comment") == NULL ||
        strstr(updated, "name = \"Benchmark \\\"A\\\"\"") == NULL ||
        strstr(updated, "top = \"renamed_top\"") == NULL ||
        strstr(updated, "default_test = \"overflow\"") == NULL ||
        strstr(updated, "name = \"paper\"") == NULL ||
        strstr(updated, "defines = [\"PAPER\"]") == NULL) {
        failed += failure("valid combined update did not preserve the manifest");
        goto cleanup_all;
    }
    result = solar_project_load(root, &project);
    if (result.status == SOLAR_STATUS_OK) result = solar_project_validate(&project);
    if (result.status != SOLAR_STATUS_OK ||
        strcmp(project.config.project.name, "Benchmark \"A\"") != 0 ||
        strcmp(project.config.synthesis.top, "renamed_top") != 0 ||
        strcmp(project.config.project.default_test, "overflow") != 0) {
        failed += failure("updated manifest did not load with requested values");
        goto cleanup_all;
    }
    solar_project_free(&project);
    solar_project_init(&project);
    changed = true;
    result = solar_config_edit_project(root, &request, &changed);
    unchanged = read_file(manifest);
    if (result.status != SOLAR_STATUS_OK || changed || unchanged == NULL ||
        strcmp(updated, unchanged) != 0) {
        failed += failure("identical update rewrote the manifest");
        goto cleanup_all;
    }
    request.project_name = NULL;
    request.synthesis_top = NULL;
    request.default_test = "missing";
    changed = true;
    result = solar_config_edit_project(root, &request, &changed);
    after_failure = read_file(manifest);
    if (result.status != SOLAR_STATUS_CONFIG_ERROR || changed ||
        after_failure == NULL || strcmp(updated, after_failure) != 0) {
        failed += failure("invalid default test did not roll back atomically");
    }
    {
        static const char legacy_manifest[] =
            "[project]\nname = \"legacy\"\ntop = \"design\"\n"
            "language = \"verilog\"\n\n"
            "[sources]\nrtl = [\"rtl/design.v\"]\n"
            "testbench = [\"tb/basic.v\"]\n\n"
            "[simulation]\nbackend = \"iverilog\"\ntop = \"basic_tb\"\n"
            "waveform = \".solar/build/sim/basic.vcd\"\n\n"
            "[synthesis]\nbackend = \"yosys\"\ntop = \"design\"\n";

        request.project_name = "renamed legacy";
        request.default_test = NULL;
        changed = true;
        if (write_file(manifest, legacy_manifest) != 0) {
            failed += failure("could not prepare format-1 rejection fixture");
        } else {
            result = solar_config_edit_project(root, &request, &changed);
            legacy_after = read_file(manifest);
            if (result.status != SOLAR_STATUS_CONFIG_ERROR || changed ||
                legacy_after == NULL ||
                strcmp(legacy_after, legacy_manifest) != 0) {
                failed += failure("format-1 edit did not remain unchanged");
            }
        }
    }

cleanup_all:
    solar_project_free(&project);
    free(after_failure);
    free(legacy_after);
    free(unchanged);
    free(updated);
    free(manifest);
    cleanup(root);
    return failed;
}

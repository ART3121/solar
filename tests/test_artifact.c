#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/artifact.h"
#include "solar/build.h"
#include "solar/filesystem.h"

#include <errno.h>
#include <fcntl.h>
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

static int write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    int failed;

    if (file == NULL) return -1;
    failed = fputs(text, file) == EOF;
    if (fclose(file) != 0) failed = 1;
    return failed ? -1 : 0;
}

static int file_equals(const char *path, const char *expected)
{
    char buffer[64];
    FILE *file = fopen(path, "r");
    size_t count;

    if (file == NULL) return 0;
    count = fread(buffer, 1U, sizeof(buffer) - 1U, file);
    buffer[count] = '\0';
    if (fclose(file) != 0) return 0;
    return strcmp(buffer, expected) == 0;
}

static int fail(const char *message)
{
    (void)fprintf(stderr, "artifact registry: %s\n", message);
    return 1;
}

static int create_sparse_file(const char *path, off_t size)
{
    int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    int failed;

    if (descriptor < 0) return -1;
    failed = ftruncate(descriptor, size) != 0;
    if (close(descriptor) != 0) failed = 1;
    return failed ? -1 : 0;
}

int main(void)
{
    char root_template[] = "/tmp/solar-artifact-XXXXXX";
    char *root = mkdtemp(root_template);
    char *temporary = NULL;
    char *assembly_temporary = NULL;
    char *published = NULL;
    char *manual = NULL;
    char *public_path = NULL;
    char *assembly_public = NULL;
    char *assembly_manual = NULL;
    char *manifest = NULL;
    char *registry = NULL;
    char *report_path = NULL;
    char *report_text = NULL;
    SolarArtifactList list;
    SolarResult result;
    size_t removed = 0U;
    bool internal_removed = false;
    int failed = 0;

    solar_artifact_list_init(&list);
    if (root == NULL) return fail("mkdtemp failed");
    manifest = join_path(root, "solar.toml");
    if (manifest == NULL || write_text(manifest,
            "[solar]\nformat = 2\n\n"
            "[project]\nname = \"artifact-test\"\nlanguage = \"verilog\"\n") != 0) {
        failed = fail("could not prepare project manifest");
        goto cleanup;
    }
    result = solar_filesystem_prepare_generated_directory(
        root, ".solar/tmp/tests/basic"
    );
    if (result.status != SOLAR_STATUS_OK) {
        failed = fail(result.diagnostic.message);
        goto cleanup;
    }
    temporary = join_path(root, ".solar/tmp/tests/basic/result.vcd");
    public_path = join_path(root, "sim/basic.vcd");
    manual = join_path(root, "sim/manual.vcd");
    if (temporary == NULL || public_path == NULL || manual == NULL ||
        write_text(temporary, "first\n") != 0) {
        failed = fail("could not prepare temporary output");
        goto cleanup;
    }
    result = solar_artifact_publish_file(
        root, temporary, "sim/basic.vcd", "waveform", "simulation",
        "basic", "iverilog", "debug", &published
    );
    if (result.status != SOLAR_STATUS_OK || !file_equals(public_path, "first\n")) {
        failed = fail("first publication failed");
        goto cleanup;
    }
    free(published);
    published = NULL;
    if (write_text(temporary, "second\n") != 0 ||
        setenv("SOLAR_TEST_DISABLE_RENAME_EXCHANGE", "1", 1) != 0) {
        failed = fail("could not configure portable publication fallback");
        goto cleanup;
    }
    result = solar_artifact_publish_file(
        root, temporary, "sim/basic.vcd", "waveform", "simulation",
        "basic", "iverilog", "release", &published
    );
    (void)unsetenv("SOLAR_TEST_DISABLE_RENAME_EXCHANGE");
    if (result.status != SOLAR_STATUS_OK || !file_equals(public_path, "second\n")) {
        failed = fail("fallback publication did not replace the registered file");
        goto cleanup;
    }
    result = solar_artifact_list_load(root, &list);
    if (result.status != SOLAR_STATUS_OK || list.count != 1U ||
        strcmp(list.items[0].path, "sim/basic.vcd") != 0 ||
        strcmp(list.items[0].profile, "release") != 0) {
        failed = fail("registry metadata is incomplete");
        goto cleanup;
    }
    solar_artifact_list_free(&list);
    free(published);
    published = NULL;
    result = solar_filesystem_prepare_generated_directory(
        root, ".solar/tmp/yanc/processor"
    );
    assembly_temporary = join_path(
        root, ".solar/tmp/yanc/processor/processor.asm"
    );
    assembly_public = join_path(root, "software/processor.asm");
    assembly_manual = join_path(root, "software/manual.asm");
    if (result.status != SOLAR_STATUS_OK || assembly_temporary == NULL ||
        assembly_public == NULL || assembly_manual == NULL ||
        write_text(assembly_temporary, "generated assembly\n") != 0) {
        failed = fail("could not prepare generated Assembly");
        goto cleanup;
    }
    result = solar_artifact_publish_file(
        root, assembly_temporary, "software/processor.asm", "waveform",
        "simulation", "", "iverilog", "debug", &published
    );
    if (result.status != SOLAR_STATUS_INVALID_ARGUMENT || published != NULL) {
        failed = fail("software accepted non-YANC artifact ownership");
        goto cleanup;
    }
    result = solar_artifact_publish_file(
        root, assembly_temporary, "software/processor.asm", "assembly",
        "generation", "generated", "yanc", "debug", &published
    );
    if (result.status != SOLAR_STATUS_OK ||
        !file_equals(assembly_public, "generated assembly\n")) {
        failed = fail("generated Assembly publication failed");
        goto cleanup;
    }
    free(published);
    published = NULL;
    if (write_text(assembly_manual, "manual assembly\n") != 0) {
        failed = fail("could not create manual Assembly");
        goto cleanup;
    }
    result = solar_artifact_publish_file(
        root, assembly_temporary, "software/manual.asm", "assembly",
        "generation", "generated", "yanc", "debug", &published
    );
    if (result.status != SOLAR_STATUS_IO_ERROR || published != NULL ||
        !file_equals(assembly_manual, "manual assembly\n")) {
        failed = fail("publication overwrote a manual Assembly file");
        goto cleanup;
    }
    result = solar_artifact_list_load(root, &list);
    if (result.status != SOLAR_STATUS_OK || list.count != 2U ||
        solar_artifact_find(&list, "assembly", "generated") == NULL) {
        failed = fail("generated Assembly was not registered");
        goto cleanup;
    }
    if (write_text(manual, "manual\n") != 0) {
        failed = fail("could not create manual public file");
        goto cleanup;
    }
    result = solar_artifact_remove_registered(root, &removed);
    if (result.status != SOLAR_STATUS_OK || removed != 2U ||
        access(public_path, F_OK) == 0 ||
        access(assembly_public, F_OK) == 0 ||
        !file_equals(manual, "manual\n") ||
        !file_equals(assembly_manual, "manual assembly\n")) {
        failed = fail("cleanup removed a manual file or retained a registered file");
    }
    solar_artifact_list_free(&list);
    registry = join_path(root, ".solar/state/artifacts.tsv");
    report_path = join_path(root, ".solar/state/last-report.txt");
    if (registry == NULL || report_path == NULL ||
        create_sparse_file(registry, (off_t)17 * 1024 * 1024) != 0) {
        failed = fail("could not prepare oversized artifact registry");
        goto cleanup;
    }
    result = solar_artifact_list_load(root, &list);
    if (result.status != SOLAR_STATUS_IO_ERROR ||
        strstr(result.diagnostic.message, "below 16 MiB") == NULL) {
        failed = fail("oversized artifact registry was accepted");
        goto cleanup;
    }
    (void)unlink(registry);
    if (create_sparse_file(report_path, (off_t)17 * 1024 * 1024) != 0) {
        failed = fail("could not prepare oversized operation report");
        goto cleanup;
    }
    result = solar_build_report_read(root, &report_text);
    if (result.status != SOLAR_STATUS_IO_ERROR || report_text != NULL ||
        strstr(result.diagnostic.message, "below 16 MiB") == NULL) {
        failed = fail("oversized operation report was accepted");
    }

cleanup:
    (void)unsetenv("SOLAR_TEST_DISABLE_RENAME_EXCHANGE");
    solar_artifact_list_free(&list);
    free(report_text);
    if (registry != NULL) (void)unlink(registry);
    if (report_path != NULL) (void)unlink(report_path);
    free(published);
    (void)solar_filesystem_clean_project(root, &internal_removed);
    if (manual != NULL) (void)unlink(manual);
    if (assembly_manual != NULL) (void)unlink(assembly_manual);
    if (assembly_public != NULL) (void)unlink(assembly_public);
    if (public_path != NULL) (void)unlink(public_path);
    {
        char *sim = join_path(root, "sim");

        if (sim != NULL) (void)rmdir(sim);
        free(sim);
    }
    {
        char *software = join_path(root, "software");

        if (software != NULL) (void)rmdir(software);
        free(software);
    }
    if (manifest != NULL) (void)unlink(manifest);
    (void)rmdir(root);
    free(report_path);
    free(registry);
    free(manifest);
    free(public_path);
    free(manual);
    free(assembly_manual);
    free(assembly_public);
    free(assembly_temporary);
    free(temporary);
    return failed;
}

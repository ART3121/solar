#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/project.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *join_path(const char *directory, const char *name)
{
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    char *path = malloc(directory_length + name_length + 2U);

    if (path == NULL) {
        return NULL;
    }
    (void)snprintf(
        path,
        directory_length + name_length + 2U,
        "%s/%s",
        directory,
        name
    );
    return path;
}

static int write_text_file(const char *path, const char *content)
{
    FILE *file = fopen(path, "w");

    if (file == NULL) {
        return -1;
    }
    if (fputs(content, file) == EOF) {
        (void)fclose(file);
        return -1;
    }
    return fclose(file);
}

static bool is_regular_file(const char *path)
{
    struct stat information;

    return stat(path, &information) == 0 && S_ISREG(information.st_mode);
}

static int report_failure(const char *test_name, const char *message)
{
    (void)fprintf(stderr, "%s: %s\n", test_name, message);
    return 1;
}

static void remove_initialized_project(const char *directory)
{
    char *manifest = join_path(directory, "solar.toml");
    char *rtl_file = join_path(directory, "rtl/counter.v");
    char *testbench_file = join_path(directory, "tb/basic.v");
    char *rtl_directory = join_path(directory, "rtl");
    char *tb_directory = join_path(directory, "tb");
    char *sim_keep = join_path(directory, "sim/.gitkeep");
    char *synth_keep = join_path(directory, "synth/.gitkeep");
    char *sim_directory = join_path(directory, "sim");
    char *synth_directory = join_path(directory, "synth");
    char *gitignore = join_path(directory, ".gitignore");
    char *readme = join_path(directory, "README.md");

    if (testbench_file != NULL) {
        (void)unlink(testbench_file);
    }
    if (rtl_file != NULL) {
        (void)unlink(rtl_file);
    }
    if (manifest != NULL) {
        (void)unlink(manifest);
    }
    if (sim_keep != NULL) (void)unlink(sim_keep);
    if (synth_keep != NULL) (void)unlink(synth_keep);
    if (gitignore != NULL) (void)unlink(gitignore);
    if (readme != NULL) (void)unlink(readme);
    if (tb_directory != NULL) {
        (void)rmdir(tb_directory);
    }
    if (rtl_directory != NULL) {
        (void)rmdir(rtl_directory);
    }
    if (sim_directory != NULL) (void)rmdir(sim_directory);
    if (synth_directory != NULL) (void)rmdir(synth_directory);
    (void)rmdir(directory);

    free(tb_directory);
    free(rtl_directory);
    free(testbench_file);
    free(rtl_file);
    free(manifest);
    free(readme);
    free(gitignore);
    free(synth_directory);
    free(sim_directory);
    free(synth_keep);
    free(sim_keep);
}

static void remove_yanc_project(
    const char *directory,
    const char *source_relative,
    bool has_include_directory
)
{
    char *manifest = join_path(directory, "solar.toml");
    char *source = join_path(directory, source_relative);
    char *source_directory = join_path(directory, "software");
    char *include_directory = has_include_directory
        ? join_path(directory, "software/include")
        : NULL;
    char *include_file = has_include_directory
        ? join_path(directory, "software/include/solar_math.hpp")
        : NULL;
    char *hardware_keep = join_path(directory, "hardware/.gitkeep");
    char *simulation_keep = join_path(directory, "simulation/.gitkeep");
    char *hardware = join_path(directory, "hardware");
    char *simulation = join_path(directory, "simulation");
    char *gitignore = join_path(directory, ".gitignore");
    char *readme = join_path(directory, "README.md");

    if (source != NULL) (void)unlink(source);
    if (include_file != NULL) (void)unlink(include_file);
    if (manifest != NULL) (void)unlink(manifest);
    if (hardware_keep != NULL) (void)unlink(hardware_keep);
    if (simulation_keep != NULL) (void)unlink(simulation_keep);
    if (gitignore != NULL) (void)unlink(gitignore);
    if (readme != NULL) (void)unlink(readme);
    if (include_directory != NULL) (void)rmdir(include_directory);
    if (source_directory != NULL) (void)rmdir(source_directory);
    if (hardware != NULL) (void)rmdir(hardware);
    if (simulation != NULL) (void)rmdir(simulation);
    (void)rmdir(directory);

    free(include_file);
    free(include_directory);
    free(source_directory);
    free(source);
    free(manifest);
    free(readme);
    free(gitignore);
    free(simulation);
    free(hardware);
    free(simulation_keep);
    free(hardware_keep);
}

static int test_initialize_load_validate_and_resolve(void)
{
    const char *test_name = "initialize, load, validate, and resolve";
    char directory_template[] = "/tmp/solar-project-load-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *manifest = NULL;
    char *rtl_file = NULL;
    char *testbench_file = NULL;
    char *resolved = NULL;
    SolarProject project;
    SolarResult result;
    int failed = 0;

    solar_project_init(&project);
    if (directory == NULL) {
        return report_failure(test_name, "mkdtemp failed");
    }

    result = solar_project_initialize(directory);
    if (result.status != SOLAR_STATUS_OK) {
        failed = report_failure(test_name, result.diagnostic.message);
        goto cleanup;
    }
    manifest = join_path(directory, "solar.toml");
    rtl_file = join_path(directory, "rtl/counter.v");
    testbench_file = join_path(directory, "tb/basic.v");
    if (manifest == NULL || rtl_file == NULL || testbench_file == NULL ||
        !is_regular_file(manifest) || !is_regular_file(rtl_file) ||
        !is_regular_file(testbench_file)) {
        failed = report_failure(test_name, "init did not create the expected files");
        goto cleanup;
    }

    result = solar_project_load(rtl_file, &project);
    if (result.status != SOLAR_STATUS_OK) {
        failed = report_failure(test_name, result.diagnostic.message);
        goto cleanup;
    }
    if (strcmp(project.root, directory) != 0 ||
        strcmp(project.manifest_path, manifest) != 0) {
        failed = report_failure(test_name, "project root was resolved incorrectly");
        goto cleanup;
    }
    if (project.discovery.rtl_files_added != 1U ||
        project.discovery.tests_added != 1U ||
        !project.discovery.synthesis_top_inferred ||
        strcmp(project.config.synthesis.top, "counter") != 0 ||
        project.config.test_count != 1U ||
        strcmp(project.config.tests[0].name, "basic") != 0 ||
        strcmp(project.config.tests[0].top, "counter_tb") != 0) {
        failed = report_failure(
            test_name, "init did not rely on conventional source discovery"
        );
        goto cleanup;
    }
    result = solar_project_validate(&project);
    if (result.status != SOLAR_STATUS_OK) {
        failed = report_failure(test_name, result.diagnostic.message);
        goto cleanup;
    }

    result = solar_project_resolve_path(&project, "tb/basic.v", &resolved);
    if (result.status != SOLAR_STATUS_OK || resolved == NULL ||
        strcmp(resolved, testbench_file) != 0) {
        failed = report_failure(test_name, "relative path resolved outside its root");
        goto cleanup;
    }
    free(resolved);
    resolved = NULL;
    result = solar_project_resolve_path(&project, "../outside.v", &resolved);
    if (result.status != SOLAR_STATUS_CONFIG_ERROR || resolved != NULL) {
        failed = report_failure(test_name, "unsafe relative path was accepted");
    }

cleanup:
    free(resolved);
    solar_project_free(&project);
    free(testbench_file);
    free(rtl_file);
    free(manifest);
    remove_initialized_project(directory);
    return failed;
}

static int test_find_root_from_nested_file(void)
{
    const char *test_name = "find root from nested file";
    char directory_template[] = "/tmp/solar-project-root-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *manifest = NULL;
    char *first_level = NULL;
    char *second_level = NULL;
    char *marker = NULL;
    char *found_root = NULL;
    SolarResult result;
    int failed = 0;

    if (directory == NULL) {
        return report_failure(test_name, "mkdtemp failed");
    }
    manifest = join_path(directory, "solar.toml");
    first_level = join_path(directory, "work");
    if (manifest == NULL || first_level == NULL) {
        failed = report_failure(test_name, "could not allocate test paths");
        goto cleanup;
    }
    second_level = join_path(first_level, "deep");
    if (second_level == NULL) {
        failed = report_failure(test_name, "could not allocate nested path");
        goto cleanup;
    }
    marker = join_path(second_level, "marker.txt");
    if (marker == NULL || write_text_file(manifest, "# root marker\n") != 0 ||
        mkdir(first_level, 0700) != 0 || mkdir(second_level, 0700) != 0 ||
        write_text_file(marker, "nested\n") != 0) {
        failed = report_failure(test_name, "could not create nested fixture");
        goto cleanup;
    }

    result = solar_project_find_root(marker, &found_root);
    if (result.status != SOLAR_STATUS_OK || found_root == NULL ||
        strcmp(found_root, directory) != 0) {
        failed = report_failure(test_name, "root discovery did not walk to solar.toml");
    }

cleanup:
    free(found_root);
    if (marker != NULL) {
        (void)unlink(marker);
    }
    if (second_level != NULL) {
        (void)rmdir(second_level);
    }
    if (first_level != NULL) {
        (void)rmdir(first_level);
    }
    if (manifest != NULL) {
        (void)unlink(manifest);
    }
    (void)rmdir(directory);
    free(marker);
    free(second_level);
    free(first_level);
    free(manifest);
    return failed;
}

static int test_initialize_refuses_overwrite(void)
{
    const char *test_name = "init refuses overwrite";
    const char sentinel[] = "user-owned manifest\n";
    char directory_template[] = "/tmp/solar-project-overwrite-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *manifest = NULL;
    char first_line[64] = {0};
    FILE *file = NULL;
    SolarResult result;
    int failed = 0;

    if (directory == NULL) {
        return report_failure(test_name, "mkdtemp failed");
    }
    result = solar_project_initialize(directory);
    if (result.status != SOLAR_STATUS_OK) {
        failed = report_failure(test_name, result.diagnostic.message);
        goto cleanup;
    }
    manifest = join_path(directory, "solar.toml");
    if (manifest == NULL || write_text_file(manifest, sentinel) != 0) {
        failed = report_failure(test_name, "could not prepare sentinel manifest");
        goto cleanup;
    }

    result = solar_project_initialize(directory);
    if (result.status != SOLAR_STATUS_CONFIG_ERROR ||
        strstr(result.diagnostic.message, "refusing to overwrite") == NULL) {
        failed = report_failure(test_name, "second init did not refuse existing files");
        goto cleanup;
    }
    file = fopen(manifest, "r");
    if (file == NULL || fgets(first_line, sizeof(first_line), file) == NULL ||
        strcmp(first_line, sentinel) != 0) {
        failed = report_failure(test_name, "init changed an existing manifest");
    }

cleanup:
    if (file != NULL) {
        (void)fclose(file);
    }
    free(manifest);
    remove_initialized_project(directory);
    return failed;
}

static int test_initialize_yanc_templates(void)
{
    static const struct {
        const char *template_name;
        const char *language;
        const char *source_relative;
        bool has_include_directory;
    } cases[] = {
        {"yanc-cmm", "cmm", "software/processor.cmm", false},
        {"yanc-cpp", "cpp", "software/processor.cpp", true},
        {"yanc-asm", "asm", "software/processor.asm", false}
    };
    const char *test_name = "initialize YANC templates";
    size_t index;
    int failed = 0;

    for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
        char directory_template[] = "/tmp/solar-project-template-XXXXXX";
        char *directory = mkdtemp(directory_template);
        char *manifest = NULL;
        char *source = NULL;
        char *include_file = NULL;
        SolarProject project;
        SolarResult result;

        solar_project_init(&project);
        if (directory == NULL) {
            return report_failure(test_name, "mkdtemp failed");
        }
        result = solar_project_initialize_template(
            directory, cases[index].template_name
        );
        if (result.status != SOLAR_STATUS_OK) {
            failed += report_failure(test_name, result.diagnostic.message);
            goto case_cleanup;
        }
        manifest = join_path(directory, "solar.toml");
        source = join_path(directory, cases[index].source_relative);
        if (cases[index].has_include_directory) {
            include_file = join_path(
                directory, "software/include/solar_math.hpp"
            );
        }
        if (manifest == NULL || source == NULL || !is_regular_file(manifest) ||
            !is_regular_file(source) ||
            (cases[index].has_include_directory &&
             !is_regular_file(include_file))) {
            failed += report_failure(test_name, "template files are incomplete");
            goto case_cleanup;
        }
        result = solar_project_load(directory, &project);
        if (result.status == SOLAR_STATUS_OK) {
            result = solar_project_validate(&project);
        }
        if (result.status != SOLAR_STATUS_OK ||
            project.config.manifest_format != 2U ||
            project.config.project.language == NULL ||
            strcmp(project.config.project.language, cases[index].language) != 0 ||
            project.config.compiler.backend == NULL ||
            strcmp(project.config.compiler.backend, "yanc") != 0 ||
            project.config.test_count != 1U ||
            strcmp(project.config.tests[0].name, "generated") != 0) {
            failed += report_failure(
                test_name,
                result.status == SOLAR_STATUS_OK
                    ? "template did not load as the expected format-2 YANC project"
                    : result.diagnostic.message
            );
            goto case_cleanup;
        }
        result = solar_project_initialize_template(
            directory, cases[index].template_name
        );
        if (result.status != SOLAR_STATUS_CONFIG_ERROR ||
            strstr(result.diagnostic.message, "refusing to overwrite") == NULL) {
            failed += report_failure(
                test_name, "second template initialization was not refused"
            );
        }

case_cleanup:
        solar_project_free(&project);
        free(include_file);
        free(source);
        free(manifest);
        remove_yanc_project(
            directory,
            cases[index].source_relative,
            cases[index].has_include_directory
        );
    }
    return failed;
}

static int test_initialize_rejects_unknown_template(void)
{
    const char *test_name = "init rejects unknown template";
    char directory_template[] = "/tmp/solar-project-template-error-XXXXXX";
    char *directory = mkdtemp(directory_template);
    SolarResult result;
    int failed = 0;

    if (directory == NULL) {
        return report_failure(test_name, "mkdtemp failed");
    }
    result = solar_project_initialize_template(directory, "unknown");
    if (result.status != SOLAR_STATUS_INVALID_ARGUMENT ||
        strstr(result.diagnostic.hint, "yanc-cmm") == NULL) {
        failed = report_failure(test_name, "unknown template was accepted");
    }
    (void)rmdir(directory);
    return failed;
}

int main(void)
{
    int failures = 0;

    failures += test_initialize_load_validate_and_resolve();
    failures += test_find_root_from_nested_file();
    failures += test_initialize_refuses_overwrite();
    failures += test_initialize_yanc_templates();
    failures += test_initialize_rejects_unknown_template();
    return failures == 0 ? 0 : 1;
}

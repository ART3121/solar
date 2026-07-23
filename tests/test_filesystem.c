#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/filesystem.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int report_failure(const char *test_name, const char *message)
{
    (void)fprintf(stderr, "%s: %s\n", test_name, message);
    return 1;
}

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

static bool file_equals(const char *path, const char *expected)
{
    char contents[256];
    size_t count;
    size_t expected_length = strlen(expected);
    bool matches;
    FILE *file = fopen(path, "r");

    if (file == NULL || expected_length > sizeof(contents)) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return false;
    }
    count = fread(contents, 1U, sizeof(contents), file);
    matches = !ferror(file) && count == expected_length &&
              memcmp(contents, expected, expected_length) == 0;
    if (fclose(file) != 0) {
        matches = false;
    }
    return matches;
}

static bool is_real_directory(const char *path)
{
    struct stat information;

    return lstat(path, &information) == 0 && S_ISDIR(information.st_mode) &&
           !S_ISLNK(information.st_mode);
}

static bool path_is_missing(const char *path)
{
    struct stat information;

    if (lstat(path, &information) == 0) {
        return false;
    }
    return errno == ENOENT;
}

static void remove_relative(const char *root, const char *relative, bool directory)
{
    char *path = join_path(root, relative);

    if (path == NULL) {
        return;
    }
    if (directory) {
        (void)rmdir(path);
    } else {
        (void)unlink(path);
    }
    free(path);
}

static void remove_layout_fixture(const char *root)
{
    remove_relative(root, ".solar/tmp/output.vvp", false);
    remove_relative(root, ".solar/tmp", true);
    remove_relative(root, ".solar", true);
    remove_relative(root, "solar-build/user-report.txt", false);
    remove_relative(root, "solar-build", true);
    remove_relative(root, "user-source.v", false);
    (void)rmdir(root);
}

static int test_layout_clean_and_preservation(void)
{
    const char *test_name = "filesystem layout and clean";
    const char *expected_directories[] = {
        ".solar"
    };
    char directory_template[] = "/tmp/solar-filesystem-layout-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *outside_path = NULL;
    char *artifact_path = NULL;
    char *solar_path = NULL;
    char *visible_build_path = NULL;
    SolarResult result;
    bool removed = false;
    size_t index;
    int failed = 0;

    if (directory == NULL) {
        return report_failure(test_name, "mkdtemp failed");
    }
    outside_path = join_path(directory, "user-source.v");
    solar_path = join_path(directory, ".solar");
    visible_build_path = join_path(directory, "solar-build");
    if (outside_path == NULL || solar_path == NULL ||
        visible_build_path == NULL ||
        write_text_file(outside_path, "user-owned source\n") != 0) {
        failed = report_failure(test_name, "could not create fixture paths");
        goto cleanup;
    }

    result = solar_filesystem_prepare_layout(directory);
    if (result.status != SOLAR_STATUS_OK) {
        failed = report_failure(test_name, result.diagnostic.message);
        goto cleanup;
    }
    for (index = 0U;
         index < sizeof(expected_directories) / sizeof(expected_directories[0]);
         index++) {
        char *path = join_path(directory, expected_directories[index]);

        if (path == NULL || !is_real_directory(path)) {
            free(path);
            failed = report_failure(test_name, "layout directory was not created safely");
            goto cleanup;
        }
        free(path);
    }

    result = solar_filesystem_prepare_generated_directory(
        directory, ".solar/tmp"
    );
    artifact_path = join_path(directory, ".solar/tmp/output.vvp");
    if (result.status != SOLAR_STATUS_OK || artifact_path == NULL ||
        write_text_file(artifact_path, "generated\n") != 0) {
        failed = report_failure(test_name, "could not create generated fixture");
        goto cleanup;
    }
    result = solar_filesystem_clean_project(directory, &removed);
    if (result.status != SOLAR_STATUS_OK || !removed ||
        !path_is_missing(solar_path) || !path_is_missing(visible_build_path)) {
        failed = report_failure(
            test_name, "clean changed the legacy root or retained .solar"
        );
        goto cleanup;
    }
    if (!file_equals(outside_path, "user-owned source\n")) {
        failed = report_failure(test_name, "clean changed a file outside .solar");
        goto cleanup;
    }

    removed = true;
    result = solar_filesystem_clean_project(directory, &removed);
    if (result.status != SOLAR_STATUS_OK || removed) {
        failed = report_failure(test_name, "second clean was not idempotent");
    }

cleanup:
    free(solar_path);
    free(visible_build_path);
    free(artifact_path);
    free(outside_path);
    remove_layout_fixture(directory);
    return failed;
}

static int test_symlinked_solar_is_refused(void)
{
    const char *test_name = "filesystem refuses symlinked .solar";
    char directory_template[] = "/tmp/solar-filesystem-symlink-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *user_directory = NULL;
    char *user_file = NULL;
    char *solar_path = NULL;
    struct stat information;
    SolarResult result;
    bool removed = true;
    int failed = 0;

    if (directory == NULL) {
        return report_failure(test_name, "mkdtemp failed");
    }
    user_directory = join_path(directory, "user-data");
    solar_path = join_path(directory, ".solar");
    if (user_directory == NULL || solar_path == NULL ||
        mkdir(user_directory, 0700) != 0) {
        failed = report_failure(test_name, "could not create symlink fixture");
        goto cleanup;
    }
    user_file = join_path(user_directory, "keep.txt");
    if (user_file == NULL || write_text_file(user_file, "keep this file\n") != 0 ||
        symlink(user_directory, solar_path) != 0) {
        failed = report_failure(test_name, "could not create .solar symlink");
        goto cleanup;
    }

    result = solar_filesystem_prepare_layout(directory);
    if (result.status != SOLAR_STATUS_IO_ERROR) {
        failed = report_failure(test_name, "layout followed a .solar symlink");
        goto cleanup;
    }
    result = solar_filesystem_clean_project(directory, &removed);
    if (result.status != SOLAR_STATUS_IO_ERROR || removed) {
        failed = report_failure(test_name, "clean accepted a .solar symlink");
        goto cleanup;
    }
    if (lstat(solar_path, &information) != 0 || !S_ISLNK(information.st_mode) ||
        !file_equals(user_file, "keep this file\n")) {
        failed = report_failure(test_name, "symlink target was changed during refusal");
    }

cleanup:
    if (solar_path != NULL) (void)unlink(solar_path);
    if (user_file != NULL) (void)unlink(user_file);
    if (user_directory != NULL) (void)rmdir(user_directory);
    (void)rmdir(directory);
    free(solar_path);
    free(user_file);
    free(user_directory);
    return failed;
}

static int test_symlinked_artifact_is_refused(void)
{
    const char *test_name = "filesystem refuses symlinked artifact";
    char directory_template[] = "/tmp/solar-filesystem-artifact-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *outside_path = NULL;
    char *artifact_path = NULL;
    SolarResult result;
    bool removed = false;
    int failed = 0;

    if (directory == NULL) {
        return report_failure(test_name, "mkdtemp failed");
    }
    outside_path = join_path(directory, "user-source.v");
    artifact_path = join_path(directory, ".solar/tmp/output.vvp");
    if (outside_path == NULL || artifact_path == NULL ||
        write_text_file(outside_path, "user-owned source\n") != 0) {
        failed = report_failure(test_name, "could not create artifact fixture");
        goto cleanup;
    }
    result = solar_filesystem_prepare_layout(directory);
    if (result.status != SOLAR_STATUS_OK ||
        solar_filesystem_prepare_generated_directory(
            directory, ".solar/tmp"
        ).status != SOLAR_STATUS_OK ||
        symlink(outside_path, artifact_path) != 0) {
        failed = report_failure(test_name, "could not prepare artifact symlink");
        goto cleanup;
    }
    result = solar_filesystem_reset_artifact(
        directory, ".solar/tmp/output.vvp"
    );
    if (result.status != SOLAR_STATUS_IO_ERROR ||
        !file_equals(outside_path, "user-owned source\n")) {
        failed = report_failure(test_name, "artifact symlink target was changed");
    }

cleanup:
    if (directory != NULL) {
        (void)solar_filesystem_clean_project(directory, &removed);
    }
    if (outside_path != NULL) (void)unlink(outside_path);
    if (directory != NULL) (void)rmdir(directory);
    free(artifact_path);
    free(outside_path);
    return failed;
}

static int test_legacy_visible_build_is_preserved(void)
{
    const char *test_name = "filesystem preserves legacy visible build";
    char directory_template[] = "/tmp/solar-filesystem-unowned-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *build_directory = NULL;
    char *user_file = NULL;
    SolarResult result;
    bool removed = true;
    int failed = 0;

    if (directory == NULL) return report_failure(test_name, "mkdtemp failed");
    build_directory = join_path(directory, "solar-build");
    if (build_directory == NULL || mkdir(build_directory, 0700) != 0) {
        failed = report_failure(test_name, "could not create build fixture");
        goto cleanup;
    }
    user_file = join_path(build_directory, "user-report.txt");
    if (user_file == NULL || write_text_file(user_file, "keep this file\n") != 0) {
        failed = report_failure(test_name, "could not create user fixture");
        goto cleanup;
    }
    result = solar_filesystem_clean_project(directory, &removed);
    if (result.status != SOLAR_STATUS_OK || removed ||
        !file_equals(user_file, "keep this file\n")) {
        failed = report_failure(
            test_name, "clean modified the legacy solar-build directory"
        );
    }

cleanup:
    if (user_file != NULL) (void)unlink(user_file);
    if (build_directory != NULL) (void)rmdir(build_directory);
    if (directory != NULL) (void)rmdir(directory);
    free(user_file);
    free(build_directory);
    return failed;
}

static int test_project_lock_contention(void)
{
    const char *test_name = "filesystem serializes project builds";
    char directory_template[] = "/tmp/solar-filesystem-lock-XXXXXX";
    char *directory = mkdtemp(directory_template);
    SolarProjectLock first_lock;
    SolarProjectLock next_lock;
    SolarResult result;
    int ready_pipe[2] = {-1, -1};
    int release_pipe[2] = {-1, -1};
    pid_t child;
    char signal_byte = '\0';
    int child_status = 0;
    bool removed = false;
    int failed = 0;

    if (directory == NULL) return report_failure(test_name, "mkdtemp failed");
    solar_project_lock_init(&first_lock);
    solar_project_lock_init(&next_lock);
    if (pipe(ready_pipe) != 0 || pipe(release_pipe) != 0) {
        failed = report_failure(test_name, "pipe failed");
        goto cleanup;
    }

    child = fork();
    if (child < 0) {
        failed = report_failure(test_name, "fork failed");
        goto cleanup;
    }
    if (child == 0) {
        (void)close(ready_pipe[0]);
        (void)close(release_pipe[1]);
        result = solar_project_lock_acquire(directory, &first_lock);
        signal_byte = result.status == SOLAR_STATUS_OK ? 'R' : 'E';
        if (write(ready_pipe[1], &signal_byte, 1U) != 1 ||
            signal_byte != 'R' || read(release_pipe[0], &signal_byte, 1U) != 1) {
            solar_project_lock_release(&first_lock);
            _exit(1);
        }
        solar_project_lock_release(&first_lock);
        _exit(0);
    }
    (void)close(ready_pipe[1]);
    ready_pipe[1] = -1;
    (void)close(release_pipe[0]);
    release_pipe[0] = -1;
    if (read(ready_pipe[0], &signal_byte, 1U) != 1 || signal_byte != 'R') {
        failed = report_failure(test_name, "the child could not acquire the lock");
        goto release_child;
    }
    result = solar_project_lock_acquire(directory, &next_lock);
    if (result.status != SOLAR_STATUS_IO_ERROR ||
        strstr(result.diagnostic.message, "already active") == NULL) {
        failed = report_failure(
            test_name, "a concurrent build was not rejected clearly"
        );
    }

release_child:
    signal_byte = 'X';
    (void)write(release_pipe[1], &signal_byte, 1U);
    if (waitpid(child, &child_status, 0) != child ||
        !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0) {
        failed = report_failure(test_name, "the lock holder did not exit cleanly");
    }
    result = solar_project_lock_acquire(directory, &next_lock);
    if (result.status != SOLAR_STATUS_OK) {
        failed = report_failure(
            test_name, "the project lock remained held after release"
        );
    }

cleanup:
    if (ready_pipe[0] >= 0) (void)close(ready_pipe[0]);
    if (ready_pipe[1] >= 0) (void)close(ready_pipe[1]);
    if (release_pipe[0] >= 0) (void)close(release_pipe[0]);
    if (release_pipe[1] >= 0) (void)close(release_pipe[1]);
    solar_project_lock_release(&next_lock);
    solar_project_lock_release(&first_lock);
    (void)solar_filesystem_clean_project(directory, &removed);
    (void)rmdir(directory);
    return failed;
}

int main(void)
{
    int failures = 0;

    failures += test_layout_clean_and_preservation();
    failures += test_symlinked_solar_is_refused();
    failures += test_symlinked_artifact_is_refused();
    failures += test_legacy_visible_build_is_preserved();
    failures += test_project_lock_contention();
    return failures == 0 ? 0 : 1;
}

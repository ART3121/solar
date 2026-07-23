#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/runner.h"

#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <signal.h>
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

typedef struct {
    unsigned activity_count;
    char output[256];
    size_t output_length;
} ObserverState;

static void observe_activity(void *user_data, uint64_t process_elapsed_ns)
{
    ObserverState *state = user_data;

    if (process_elapsed_ns > 0U) state->activity_count++;
}

static void observe_output(
    void *user_data,
    bool standard_error,
    const char *data,
    size_t length
)
{
    ObserverState *state = user_data;
    size_t available = sizeof(state->output) - state->output_length - 1U;
    size_t stored = length < available ? length : available;

    (void)standard_error;
    (void)memcpy(state->output + state->output_length, data, stored);
    state->output_length += stored;
    state->output[state->output_length] = '\0';
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

static int test_successful_capture(const char *helper_path)
{
    const char *test_name = "runner successful capture";
    const char *arguments[] = {helper_path, "success", NULL};
    SolarProcessSpec specification = {
        helper_path, arguments, NULL, NULL, NULL, NULL, 0U, NULL
    };
    SolarProcessResult process_result;
    SolarResult result;
    int failed = 0;

    solar_process_result_init(&process_result);
    result = solar_runner_run(&specification, &process_result);
    if (result.status != SOLAR_STATUS_OK) {
        failed = report_failure(test_name, result.diagnostic.message);
    } else if (process_result.outcome != SOLAR_PROCESS_EXITED ||
               process_result.exit_code != 0 ||
               process_result.stdout_text == NULL ||
               strcmp(process_result.stdout_text, "helper stdout\n") != 0 ||
               process_result.stderr_text == NULL ||
               strcmp(process_result.stderr_text, "helper stderr\n") != 0 ||
               process_result.stdout_truncated ||
               process_result.stderr_truncated ||
               process_result.duration_ns == 0U) {
        failed = report_failure(test_name, "captured process result was incorrect");
    }
    solar_process_result_free(&process_result);
    return failed;
}

static int test_nonzero_exit(const char *helper_path)
{
    const char *test_name = "runner nonzero exit";
    const char *arguments[] = {helper_path, "fail", NULL};
    SolarProcessSpec specification = {
        helper_path, arguments, NULL, NULL, NULL, NULL, 0U, NULL
    };
    SolarProcessResult process_result;
    SolarResult result;
    int failed = 0;

    solar_process_result_init(&process_result);
    result = solar_runner_run(&specification, &process_result);
    if (result.status != SOLAR_STATUS_PROCESS_FAILED ||
        process_result.outcome != SOLAR_PROCESS_EXITED ||
        process_result.exit_code != 23 ||
        process_result.stdout_text == NULL ||
        strcmp(process_result.stdout_text, "failure stdout\n") != 0 ||
        process_result.stderr_text == NULL ||
        strcmp(process_result.stderr_text, "failure stderr\n") != 0 ||
        process_result.duration_ns == 0U ||
        strstr(result.diagnostic.message, "status 23") == NULL ||
        result.diagnostic.hint[0] == '\0') {
        failed = report_failure(
            test_name, "nonzero status and output were not preserved"
        );
    }
    solar_process_result_free(&process_result);
    return failed;
}

static int test_large_output_is_drained_and_bounded(const char *helper_path)
{
    const char *test_name = "runner large output capture";
    const char *arguments[] = {helper_path, "large", NULL};
    SolarProcessSpec specification = {
        helper_path, arguments, NULL, NULL, NULL, NULL, 0U, NULL
    };
    SolarProcessResult process_result;
    SolarResult result;
    int failed = 0;

    solar_process_result_init(&process_result);
    result = solar_runner_run(&specification, &process_result);
    if (result.status != SOLAR_STATUS_OK ||
        process_result.stdout_text == NULL ||
        process_result.stderr_text == NULL ||
        strlen(process_result.stdout_text) != 1024U * 1024U ||
        strlen(process_result.stderr_text) != 1024U * 1024U ||
        !process_result.stdout_truncated ||
        !process_result.stderr_truncated) {
        failed = report_failure(
            test_name, "large child output blocked or exceeded capture bounds"
        );
    }
    solar_process_result_free(&process_result);
    return failed;
}

static int test_missing_executable(void)
{
    const char *test_name = "runner missing executable";
    char directory_template[] = "/tmp/solar-runner-missing-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *missing_path = NULL;
    const char *arguments[2];
    SolarProcessSpec specification = {0};
    SolarProcessResult process_result;
    SolarResult result;
    int failed = 0;

    solar_process_result_init(&process_result);
    if (directory == NULL) {
        return report_failure(test_name, "mkdtemp failed");
    }
    missing_path = join_path(directory, "not-an-executable");
    if (missing_path == NULL) {
        failed = report_failure(test_name, "could not allocate executable path");
        goto cleanup;
    }

    arguments[0] = missing_path;
    arguments[1] = NULL;
    specification.executable = missing_path;
    specification.argv = arguments;
    result = solar_runner_run(&specification, &process_result);
    if (result.status != SOLAR_STATUS_TOOL_MISSING ||
        process_result.outcome != SOLAR_PROCESS_EXEC_FAILED ||
        process_result.exec_errno != ENOENT ||
        strstr(result.diagnostic.message, missing_path) == NULL ||
        result.diagnostic.hint[0] == '\0') {
        failed = report_failure(test_name, "missing executable was misclassified");
    }

cleanup:
    solar_process_result_free(&process_result);
    free(missing_path);
    (void)rmdir(directory);
    return failed;
}

static int test_signal_termination(const char *helper_path)
{
    const char *test_name = "runner signal termination";
    const char *arguments[] = {helper_path, "signal", NULL};
    SolarProcessSpec specification = {
        helper_path, arguments, NULL, NULL, NULL, NULL, 0U, NULL
    };
    SolarProcessResult process_result;
    SolarResult result;
    int failed = 0;

    solar_process_result_init(&process_result);
    result = solar_runner_run(&specification, &process_result);
    if (result.status != SOLAR_STATUS_PROCESS_FAILED ||
        process_result.outcome != SOLAR_PROCESS_SIGNALED ||
        process_result.term_signal != SIGTERM ||
        strstr(result.diagnostic.message, "signal") == NULL) {
        failed = report_failure(test_name, "signal status was not preserved");
    }
    solar_process_result_free(&process_result);
    return failed;
}

static bool watched_mask_matches(const sigset_t *left, const sigset_t *right)
{
    return sigismember(left, SIGINT) == sigismember(right, SIGINT) &&
           sigismember(left, SIGTERM) == sigismember(right, SIGTERM) &&
           sigismember(left, SIGHUP) == sigismember(right, SIGHUP);
}

static int count_open_descriptors(void)
{
    DIR *directory = opendir("/proc/self/fd");
    struct dirent *entry;
    int count = 0;
    int read_errno;

    if (directory == NULL) {
        return -1;
    }
    errno = 0;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 &&
            strcmp(entry->d_name, "..") != 0) {
            count++;
        }
    }
    read_errno = errno;
    if (closedir(directory) != 0 || read_errno != 0) {
        return -1;
    }
    return count;
}

static int test_parent_signal_is_forwarded(const char *helper_path)
{
    const char *test_name = "runner parent signal forwarding";
    char parent_text[32];
    const char *arguments[] = {helper_path, "block", parent_text, NULL};
    SolarProcessSpec specification = {
        helper_path, arguments, NULL, NULL, NULL, NULL, 0U, NULL
    };
    ObserverState observer_state = {0};
    SolarProgressObserver observer = {0};
    SolarProcessResult process_result;
    SolarResult result;
    sigset_t mask_before;
    sigset_t mask_after;
    struct sigaction action_before;
    struct sigaction action_after;
    char *end = NULL;
    long helper_value;
    int child_status = 0;
    int descriptors_before;
    int descriptors_after;
    int failed = 0;

    (void)snprintf(parent_text, sizeof(parent_text), "%ld", (long)getpid());
    observer.activity = observe_activity;
    observer.user_data = &observer_state;
    specification.progress_observer = &observer;
    descriptors_before = count_open_descriptors();
    if (descriptors_before < 0 ||
        sigprocmask(SIG_BLOCK, NULL, &mask_before) != 0 ||
        sigaction(SIGINT, NULL, &action_before) != 0) {
        return report_failure(test_name, "could not inspect initial process state");
    }

    solar_process_result_init(&process_result);
    result = solar_runner_run(&specification, &process_result);
    descriptors_after = count_open_descriptors();
    if (descriptors_after < 0 ||
        sigprocmask(SIG_BLOCK, NULL, &mask_after) != 0 ||
        sigaction(SIGINT, NULL, &action_after) != 0) {
        failed = report_failure(test_name, "could not inspect restored process state");
        goto cleanup;
    }
    if (result.status != SOLAR_STATUS_PROCESS_FAILED ||
        process_result.outcome != SOLAR_PROCESS_SIGNALED ||
        process_result.term_signal != SIGINT ||
        process_result.stdout_text == NULL ||
        observer_state.activity_count == 0U ||
        strstr(result.diagnostic.message, "signal 2") == NULL) {
        failed = report_failure(
            test_name, "parent interruption was not forwarded and classified"
        );
        goto cleanup;
    }
    if (!watched_mask_matches(&mask_before, &mask_after)) {
        failed = report_failure(test_name, "runner did not restore signal mask");
        goto cleanup;
    }
    if (action_before.sa_handler != action_after.sa_handler ||
        descriptors_before != descriptors_after) {
        failed = report_failure(
            test_name, "runner changed a signal disposition or leaked a descriptor"
        );
        goto cleanup;
    }

    errno = 0;
    helper_value = strtol(process_result.stdout_text, &end, 10);
    if (errno != 0 || end == process_result.stdout_text || *end != '\n' ||
        end[1] != '\0' ||
        helper_value <= 0) {
        failed = report_failure(test_name, "helper did not report a valid pid");
        goto cleanup;
    }
    errno = 0;
    if (waitpid((pid_t)helper_value, &child_status, WNOHANG) != -1 ||
        errno != ECHILD) {
        failed = report_failure(test_name, "runner did not reap interrupted child");
    }

cleanup:
    solar_process_result_free(&process_result);
    return failed;
}

static int restore_environment_value(const char *name, const char *value)
{
    if (value == NULL) {
        return unsetenv(name);
    }
    return setenv(name, value, 1);
}

static int test_child_environment_addition(const char *helper_path)
{
    const char *test_name = "runner child environment addition";
    const char *variable_name = "SOLAR_RUNNER_CHILD_ONLY_TEST";
    const char *arguments[] = {
        helper_path, "environment", variable_name, NULL
    };
    const SolarEnvironmentAddition additions[] = {
        {variable_name, "child value with spaces"}
    };
    SolarProcessSpec specification = {
        helper_path, arguments, NULL, NULL, NULL, additions, 1U, NULL
    };
    SolarProcessResult process_result;
    SolarResult result;
    const char *initial_value = getenv(variable_name);
    char *saved_value = initial_value == NULL ? NULL : strdup(initial_value);
    int failed = 0;

    if (initial_value != NULL && saved_value == NULL) {
        return report_failure(test_name, "could not preserve parent environment");
    }
    if (setenv(variable_name, "parent value", 1) != 0) {
        free(saved_value);
        return report_failure(test_name, "could not prepare parent environment");
    }

    solar_process_result_init(&process_result);
    result = solar_runner_run(&specification, &process_result);
    if (result.status != SOLAR_STATUS_OK ||
        process_result.outcome != SOLAR_PROCESS_EXITED ||
        process_result.exit_code != 0 ||
        process_result.stdout_text == NULL ||
        strcmp(process_result.stdout_text, "child value with spaces\n") != 0 ||
        getenv(variable_name) == NULL ||
        strcmp(getenv(variable_name), "parent value") != 0) {
        failed = report_failure(
            test_name, "child value leaked or was not passed to the helper"
        );
    }
    solar_process_result_free(&process_result);
    if (restore_environment_value(variable_name, saved_value) != 0 && failed == 0) {
        failed = report_failure(test_name, "could not restore test environment");
    }
    free(saved_value);
    return failed;
}

static int test_invalid_environment_name(const char *helper_path)
{
    const char *test_name = "runner invalid environment name";
    const char *arguments[] = {helper_path, "success", NULL};
    const SolarEnvironmentAddition additions[] = {
        {"INVALID-NAME", "value"}
    };
    SolarProcessSpec specification = {
        helper_path, arguments, NULL, NULL, NULL, additions, 1U, NULL
    };
    SolarProcessResult process_result;
    SolarResult result;
    int failed = 0;

    solar_process_result_init(&process_result);
    result = solar_runner_run(&specification, &process_result);
    if (result.status != SOLAR_STATUS_INVALID_ARGUMENT ||
        process_result.outcome != SOLAR_PROCESS_NOT_STARTED ||
        strstr(result.diagnostic.message, "invalid name") == NULL) {
        failed = report_failure(test_name, "unsafe environment name was accepted");
    }
    solar_process_result_free(&process_result);
    return failed;
}

static int test_log_paths_with_spaces(const char *helper_path)
{
    const char *test_name = "runner log paths with spaces";
    char directory_template[] = "/tmp/solar-runner-logs-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *log_directory = NULL;
    char *stdout_path = NULL;
    char *stderr_path = NULL;
    const char *arguments[] = {helper_path, "success", NULL};
    SolarProcessSpec specification = {0};
    ObserverState observer_state = {0};
    SolarProgressObserver observer = {0};
    SolarProcessResult process_result;
    SolarResult result;
    int failed = 0;

    solar_process_result_init(&process_result);
    if (directory == NULL) {
        return report_failure(test_name, "mkdtemp failed");
    }
    log_directory = join_path(directory, "logs with spaces");
    if (log_directory == NULL || mkdir(log_directory, 0700) != 0) {
        failed = report_failure(test_name, "could not create spaced log directory");
        goto cleanup;
    }
    stdout_path = join_path(log_directory, "standard output.log");
    stderr_path = join_path(log_directory, "standard error.log");
    if (stdout_path == NULL || stderr_path == NULL) {
        failed = report_failure(test_name, "could not allocate log paths");
        goto cleanup;
    }

    specification.executable = helper_path;
    specification.argv = arguments;
    specification.stdout_log_path = stdout_path;
    specification.stderr_log_path = stderr_path;
    observer.tool_output = observe_output;
    observer.user_data = &observer_state;
    specification.progress_observer = &observer;
    result = solar_runner_run(&specification, &process_result);
    if (result.status != SOLAR_STATUS_OK ||
        process_result.outcome != SOLAR_PROCESS_EXITED ||
        process_result.exit_code != 0) {
        failed = report_failure(test_name, result.diagnostic.message);
    } else if (process_result.stdout_text != NULL ||
               process_result.stderr_text != NULL ||
               !file_equals(stdout_path, "helper stdout\n") ||
               !file_equals(stderr_path, "helper stderr\n") ||
               strstr(observer_state.output, "helper stdout") == NULL ||
               strstr(observer_state.output, "helper stderr") == NULL) {
        failed = report_failure(test_name, "spaced log paths lost process output");
    }

cleanup:
    solar_process_result_free(&process_result);
    if (stderr_path != NULL) (void)unlink(stderr_path);
    if (stdout_path != NULL) (void)unlink(stdout_path);
    if (log_directory != NULL) (void)rmdir(log_directory);
    (void)rmdir(directory);
    free(stderr_path);
    free(stdout_path);
    free(log_directory);
    return failed;
}

static int test_observed_long_process(const char *helper_path)
{
    const char *test_name = "runner observed long process";
    const char *arguments[] = {helper_path, "slow", NULL};
    ObserverState state = {0};
    SolarProgressObserver observer = {0};
    SolarProcessSpec specification = {0};
    SolarProcessResult process_result;
    SolarResult result;
    int failed = 0;

    observer.activity = observe_activity;
    observer.tool_output = observe_output;
    observer.user_data = &state;
    specification.executable = helper_path;
    specification.argv = arguments;
    specification.progress_observer = &observer;
    solar_process_result_init(&process_result);
    result = solar_runner_run(&specification, &process_result);
    if (result.status != SOLAR_STATUS_OK || state.activity_count < 3U ||
        strstr(state.output, "slow start") == NULL ||
        strstr(state.output, "slow done") == NULL ||
        process_result.stdout_text == NULL ||
        strstr(process_result.stdout_text, "slow done") == NULL) {
        failed = report_failure(
            test_name, "activity/output callbacks did not remain live"
        );
    }
    solar_process_result_free(&process_result);
    return failed;
}

int main(int argc, char *argv[])
{
    int failures = 0;

    if (argc != 2) {
        return report_failure(
            "runner tests", "expected the runner_helper executable path"
        );
    }
    failures += test_successful_capture(argv[1]);
    failures += test_large_output_is_drained_and_bounded(argv[1]);
    failures += test_nonzero_exit(argv[1]);
    failures += test_missing_executable();
    failures += test_signal_termination(argv[1]);
    failures += test_parent_signal_is_forwarded(argv[1]);
    failures += test_child_environment_addition(argv[1]);
    failures += test_invalid_environment_name(argv[1]);
    failures += test_log_paths_with_spaces(argv[1]);
    failures += test_observed_long_process(argv[1]);
    return failures == 0 ? 0 : 1;
}

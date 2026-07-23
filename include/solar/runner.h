#ifndef SOLAR_RUNNER_H
#define SOLAR_RUNNER_H

#include "solar/progress.h"
#include "solar/result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SOLAR_PROCESS_NOT_STARTED = 0,
    SOLAR_PROCESS_EXITED,
    SOLAR_PROCESS_SIGNALED,
    SOLAR_PROCESS_EXEC_FAILED
} SolarProcessOutcome;

typedef struct {
    const char *name;
    const char *value;
} SolarEnvironmentAddition;

typedef struct {
    const char *executable;
    const char *const *argv;
    const char *working_directory;
    const char *stdout_log_path;
    const char *stderr_log_path;
    const SolarEnvironmentAddition *environment_additions;
    size_t environment_addition_count;
    const SolarProgressObserver *progress_observer;
} SolarProcessSpec;

typedef struct {
    SolarProcessOutcome outcome;
    int exit_code;
    int term_signal;
    int exec_errno;
    char *stdout_text;
    char *stderr_text;
    bool stdout_truncated;
    bool stderr_truncated;
    /* Monotonic wall-clock time from process preparation through reap/output. */
    uint64_t duration_ns;
} SolarProcessResult;

void solar_process_result_init(SolarProcessResult *result);
void solar_process_result_free(SolarProcessResult *result);

/*
 * A null log path captures that stream in memory. Captured strings are owned by
 * result and released with solar_process_result_free(). While the call is
 * active, SIGINT, SIGTERM, and SIGHUP are forwarded to the tool's process
 * group. The caller's signal mask and dispositions are unchanged on return.
 * An interruption is reported as SOLAR_PROCESS_SIGNALED with term_signal set.
 * Environment additions override values only in the child. Names use the
 * portable [A-Za-z_][A-Za-z0-9_]* form; values and the array remain caller-owned.
 * A borrowed progress observer receives periodic activity callbacks. When it
 * requests tool output, the runner tees each stream to its configured log and
 * the callback without invoking a shell or creating a reader thread.
 */
SolarResult solar_runner_run(
    const SolarProcessSpec *specification,
    SolarProcessResult *process_result
);

#endif

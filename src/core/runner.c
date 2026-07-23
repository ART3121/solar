#include "solar/runner.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SOLAR_CAPTURE_LIMIT ((size_t)1024U * (size_t)1024U)

typedef struct {
    int parent_read;
    int child_write;
    int log_write;
    bool captured;
} ProcessStream;

typedef struct {
    char *text;
    size_t length;
    bool truncated;
    bool allocation_failed;
} CaptureBuffer;

typedef struct {
    int descriptor;
    sigset_t watched_signals;
    sigset_t previous_mask;
    bool mask_changed;
} SignalMonitor;

static uint64_t monotonic_nanoseconds(void)
{
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0U;
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
        (uint64_t)value.tv_nsec;
}

static SolarResult runner_error(
    SolarStatus status,
    const char *hint,
    const char *format,
    ...
)
{
    SolarResult result;
    va_list arguments;

    result.status = status;
    result.diagnostic.severity = SOLAR_DIAGNOSTIC_ERROR;
    va_start(arguments, format);
    (void)vsnprintf(
        result.diagnostic.message,
        sizeof(result.diagnostic.message),
        format,
        arguments
    );
    va_end(arguments);
    (void)snprintf(
        result.diagnostic.hint,
        sizeof(result.diagnostic.hint),
        "%s",
        hint == NULL ? "" : hint
    );
    return result;
}

void solar_process_result_init(SolarProcessResult *result)
{
    if (result == NULL) {
        return;
    }
    result->outcome = SOLAR_PROCESS_NOT_STARTED;
    result->exit_code = -1;
    result->term_signal = 0;
    result->exec_errno = 0;
    result->stdout_text = NULL;
    result->stderr_text = NULL;
    result->stdout_truncated = false;
    result->stderr_truncated = false;
    result->duration_ns = 0U;
}

void solar_process_result_free(SolarProcessResult *result)
{
    if (result == NULL) {
        return;
    }
    free(result->stdout_text);
    free(result->stderr_text);
    solar_process_result_init(result);
}

static int set_close_on_exec(int descriptor)
{
    int flags = fcntl(descriptor, F_GETFD);

    if (flags < 0) {
        return -1;
    }
    return fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC);
}

static void close_stream(ProcessStream *stream)
{
    if (stream->parent_read >= 0) {
        (void)close(stream->parent_read);
        stream->parent_read = -1;
    }
    if (stream->child_write >= 0) {
        (void)close(stream->child_write);
        stream->child_write = -1;
    }
    if (stream->log_write >= 0) {
        (void)close(stream->log_write);
        stream->log_write = -1;
    }
}

static SolarResult prepare_stream(
    const char *log_path,
    const char *stream_name,
    bool observe_output,
    ProcessStream *stream
)
{
    int descriptors[2];

    stream->parent_read = -1;
    stream->child_write = -1;
    stream->log_write = -1;
    stream->captured = log_path == NULL;
    if (log_path != NULL) {
        stream->log_write = open(
            log_path,
            O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
            0644
        );
        if (stream->log_write < 0) {
            return runner_error(
                SOLAR_STATUS_IO_ERROR,
                "ensure the log directory exists and is not a symlink",
                "cannot open %s log %s: %s",
                stream_name,
                log_path,
                strerror(errno)
            );
        }
        if (!observe_output) {
            stream->child_write = stream->log_write;
            stream->log_write = -1;
            return solar_result_ok();
        }
    }

    if (pipe(descriptors) != 0) {
        return runner_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "reduce open files and try again",
            "cannot create %s capture pipe: %s",
            stream_name,
            strerror(errno)
        );
    }
    if (set_close_on_exec(descriptors[0]) != 0 ||
        set_close_on_exec(descriptors[1]) != 0) {
        int saved_errno = errno;
        (void)close(descriptors[0]);
        (void)close(descriptors[1]);
        return runner_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "reduce open files and try again",
            "cannot configure %s capture pipe: %s",
            stream_name,
            strerror(saved_errno)
        );
    }
    stream->parent_read = descriptors[0];
    stream->child_write = descriptors[1];
    return solar_result_ok();
}

static int prepare_exec_pipe(int descriptors[2])
{
    if (pipe(descriptors) != 0) {
        return -1;
    }
    if (set_close_on_exec(descriptors[0]) != 0 ||
        set_close_on_exec(descriptors[1]) != 0) {
        int saved_errno = errno;
        (void)close(descriptors[0]);
        (void)close(descriptors[1]);
        descriptors[0] = -1;
        descriptors[1] = -1;
        errno = saved_errno;
        return -1;
    }
    return 0;
}

static int prepare_signal_monitor(SignalMonitor *monitor)
{
    monitor->descriptor = -1;
    monitor->mask_changed = false;
    if (sigemptyset(&monitor->watched_signals) != 0 ||
        sigaddset(&monitor->watched_signals, SIGINT) != 0 ||
        sigaddset(&monitor->watched_signals, SIGTERM) != 0 ||
        sigaddset(&monitor->watched_signals, SIGHUP) != 0) {
        return -1;
    }
    if (sigprocmask(
            SIG_BLOCK, &monitor->watched_signals, &monitor->previous_mask
        ) != 0) {
        return -1;
    }
    monitor->mask_changed = true;
    monitor->descriptor = signalfd(
        -1, &monitor->watched_signals, SFD_CLOEXEC | SFD_NONBLOCK
    );
    if (monitor->descriptor < 0) {
        int saved_errno = errno;
        (void)sigprocmask(SIG_SETMASK, &monitor->previous_mask, NULL);
        monitor->mask_changed = false;
        errno = saved_errno;
        return -1;
    }
    return 0;
}

static int close_signal_monitor(SignalMonitor *monitor)
{
    int saved_errno = 0;

    if (monitor->descriptor >= 0) {
        (void)close(monitor->descriptor);
        monitor->descriptor = -1;
    }
    if (monitor->mask_changed &&
        sigprocmask(SIG_SETMASK, &monitor->previous_mask, NULL) != 0) {
        saved_errno = errno;
    }
    monitor->mask_changed = false;
    if (saved_errno != 0) {
        errno = saved_errno;
        return -1;
    }
    return 0;
}

static void report_child_error(int descriptor, int error_number)
{
    const unsigned char *cursor = (const unsigned char *)&error_number;
    size_t remaining = sizeof(error_number);

    while (remaining > 0U) {
        ssize_t written = write(descriptor, cursor, remaining);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            break;
        }
        cursor += written;
        remaining -= (size_t)written;
    }
}

static bool environment_name_is_valid(const char *name)
{
    const unsigned char *cursor = (const unsigned char *)name;

    if (cursor == NULL ||
        !((cursor[0] >= (unsigned char)'A' &&
           cursor[0] <= (unsigned char)'Z') ||
          (cursor[0] >= (unsigned char)'a' &&
           cursor[0] <= (unsigned char)'z') ||
          cursor[0] == (unsigned char)'_')) {
        return false;
    }
    cursor++;
    while (*cursor != '\0') {
        if (!((*cursor >= (unsigned char)'A' &&
               *cursor <= (unsigned char)'Z') ||
              (*cursor >= (unsigned char)'a' &&
               *cursor <= (unsigned char)'z') ||
              (*cursor >= (unsigned char)'0' &&
               *cursor <= (unsigned char)'9') ||
              *cursor == (unsigned char)'_')) {
            return false;
        }
        cursor++;
    }
    return true;
}

static SolarResult validate_environment_additions(
    const SolarProcessSpec *specification
)
{
    size_t index;

    if (specification->environment_addition_count > 0U &&
        specification->environment_additions == NULL) {
        return runner_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "provide one name/value entry for each environment addition",
            "process environment additions are missing"
        );
    }
    for (index = 0U; index < specification->environment_addition_count; index++) {
        const SolarEnvironmentAddition *addition =
            &specification->environment_additions[index];

        if (!environment_name_is_valid(addition->name)) {
            return runner_error(
                SOLAR_STATUS_INVALID_ARGUMENT,
                "use a name matching [A-Za-z_][A-Za-z0-9_]*",
                "process environment addition %zu has an invalid name",
                index
            );
        }
        if (addition->value == NULL) {
            return runner_error(
                SOLAR_STATUS_INVALID_ARGUMENT,
                "provide a non-null value for every environment addition",
                "process environment addition %s has no value",
                addition->name
            );
        }
    }
    return solar_result_ok();
}

static int apply_environment_additions(const SolarProcessSpec *specification)
{
    size_t index;

    for (index = 0U; index < specification->environment_addition_count; index++) {
        const SolarEnvironmentAddition *addition =
            &specification->environment_additions[index];

        if (setenv(addition->name, addition->value, 1) != 0) {
            return -1;
        }
    }
    return 0;
}

static _Noreturn void child_setup_failed(
    ProcessStream *standard_output,
    ProcessStream *standard_error,
    int exec_descriptor,
    int error_number,
    bool stdout_redirected,
    bool stderr_redirected
)
{
    report_child_error(exec_descriptor, error_number);
    if (standard_output->child_write >= 0 &&
        standard_output->child_write != STDOUT_FILENO) {
        (void)close(standard_output->child_write);
    }
    if (standard_error->child_write >= 0 &&
        standard_error->child_write != STDERR_FILENO) {
        (void)close(standard_error->child_write);
    }
    if (stdout_redirected) (void)close(STDOUT_FILENO);
    if (stderr_redirected) (void)close(STDERR_FILENO);
    (void)close(exec_descriptor);
    _exit(127);
}

static _Noreturn void run_child(
    const SolarProcessSpec *specification,
    ProcessStream *standard_output,
    ProcessStream *standard_error,
    int exec_pipe[2],
    const SignalMonitor *signal_monitor
)
{
    int error_number;
    bool stdout_redirected = false;
    bool stderr_redirected = false;

    (void)close(exec_pipe[0]);
    (void)close(signal_monitor->descriptor);
    if (standard_output->parent_read >= 0) {
        (void)close(standard_output->parent_read);
    }
    if (standard_error->parent_read >= 0) {
        (void)close(standard_error->parent_read);
    }
    /* In observed mode the parent owns the log descriptors and tees pipe
     * output into them. The child must not keep those descriptors alive. */
    if (standard_output->log_write >= 0) {
        (void)close(standard_output->log_write);
        standard_output->log_write = -1;
    }
    if (standard_error->log_write >= 0) {
        (void)close(standard_error->log_write);
        standard_error->log_write = -1;
    }
    /* A separate group lets one terminal signal reach tool subprocesses too. */
    if (setpgid(0, 0) != 0) {
        error_number = errno;
        child_setup_failed(
            standard_output, standard_error, exec_pipe[1], error_number,
            false, false
        );
    }
    if (sigprocmask(SIG_SETMASK, &signal_monitor->previous_mask, NULL) != 0) {
        error_number = errno;
        child_setup_failed(
            standard_output, standard_error, exec_pipe[1], error_number,
            false, false
        );
    }
    if (apply_environment_additions(specification) != 0) {
        error_number = errno;
        child_setup_failed(
            standard_output, standard_error, exec_pipe[1], error_number,
            false, false
        );
    }
    if (specification->working_directory != NULL &&
        chdir(specification->working_directory) != 0) {
        error_number = errno;
        child_setup_failed(
            standard_output, standard_error, exec_pipe[1], error_number,
            false, false
        );
    }
    if (dup2(standard_output->child_write, STDOUT_FILENO) < 0) {
        error_number = errno;
        child_setup_failed(
            standard_output, standard_error, exec_pipe[1], error_number,
            false, false
        );
    }
    stdout_redirected = true;
    if (standard_output->child_write != STDOUT_FILENO) {
        (void)close(standard_output->child_write);
        standard_output->child_write = -1;
    }
    if (dup2(standard_error->child_write, STDERR_FILENO) < 0) {
        error_number = errno;
        child_setup_failed(
            standard_output, standard_error, exec_pipe[1], error_number,
            stdout_redirected, false
        );
    }
    stderr_redirected = true;
    if (standard_error->child_write != STDERR_FILENO) {
        (void)close(standard_error->child_write);
        standard_error->child_write = -1;
    }

    execvp(
        specification->executable,
        (char *const *)(uintptr_t)specification->argv
    );
    error_number = errno;
    child_setup_failed(
        standard_output, standard_error, exec_pipe[1], error_number,
        stdout_redirected, stderr_redirected
    );
}

static int read_exec_error(int descriptor, int *error_number)
{
    unsigned char *cursor = (unsigned char *)error_number;
    size_t received = 0U;

    *error_number = 0;
    while (received < sizeof(*error_number)) {
        ssize_t count = read(
            descriptor, cursor + received, sizeof(*error_number) - received
        );
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            return -1;
        }
        if (count == 0) {
            return received == 0U ? 0 : -1;
        }
        received += (size_t)count;
    }
    return 1;
}

static void append_capture(
    CaptureBuffer *buffer,
    const char *data,
    size_t data_length
)
{
    size_t available;
    size_t stored_length;
    char *expanded;

    if (buffer->allocation_failed) {
        return;
    }
    if (buffer->length >= SOLAR_CAPTURE_LIMIT) {
        buffer->truncated = true;
        return;
    }
    available = SOLAR_CAPTURE_LIMIT - buffer->length;
    stored_length = data_length < available ? data_length : available;
    if (stored_length < data_length) {
        buffer->truncated = true;
    }
    expanded = realloc(buffer->text, buffer->length + stored_length + 1U);
    if (expanded == NULL) {
        buffer->allocation_failed = true;
        return;
    }
    buffer->text = expanded;
    (void)memcpy(buffer->text + buffer->length, data, stored_length);
    buffer->length += stored_length;
    buffer->text[buffer->length] = '\0';
}

static int write_log_chunk(int descriptor, const char *data, size_t length)
{
    size_t offset = 0U;

    while (offset < length) {
        ssize_t written = write(descriptor, data + offset, length - offset);

        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return -1;
        offset += (size_t)written;
    }
    return 0;
}

static int read_stream_descriptor(
    ProcessStream *stream,
    CaptureBuffer *buffer,
    const SolarProgressObserver *observer,
    bool standard_error
)
{
    char chunk[4096];
    ssize_t count;

    do {
        count = read(stream->parent_read, chunk, sizeof(chunk));
    } while (count < 0 && errno == EINTR);
    if (count > 0) {
        if (stream->log_write >= 0 &&
            write_log_chunk(stream->log_write, chunk, (size_t)count) != 0) {
            return -1;
        }
        if (stream->captured) append_capture(buffer, chunk, (size_t)count);
        solar_progress_tool_output(
            observer, standard_error, chunk, (size_t)count
        );
        return 0;
    }
    if (count == 0) {
        (void)close(stream->parent_read);
        stream->parent_read = -1;
        return 0;
    }
    return -1;
}

static int forward_signal_to_process_group(pid_t process, int signal_number)
{
    if (kill(-process, signal_number) == 0) {
        return 0;
    }
    if (errno != ESRCH) {
        return -1;
    }

    /* The child may not have completed setpgid() yet. Its blocked mask keeps
     * this fallback signal pending until it restores the caller's mask. */
    if (kill(process, signal_number) == 0 || errno == ESRCH) {
        return 0;
    }
    return -1;
}

static int consume_forwarded_signals(
    int signal_descriptor,
    pid_t process,
    int *forwarded_signal
)
{
    struct signalfd_siginfo signal_info;

    for (;;) {
        ssize_t count = read(
            signal_descriptor, &signal_info, sizeof(signal_info)
        );
        if (count == (ssize_t)sizeof(signal_info)) {
            int signal_number = (int)signal_info.ssi_signo;

            if (*forwarded_signal == 0) {
                *forwarded_signal = signal_number;
            }
            if (forward_signal_to_process_group(process, signal_number) != 0) {
                return -1;
            }
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return 0;
        }
        errno = EIO;
        return -1;
    }
}

static int reap_process_nonblocking(
    pid_t process,
    int *wait_status,
    bool *reaped
)
{
    pid_t waited;

    if (*reaped) {
        return 0;
    }
    do {
        waited = waitpid(process, wait_status, WNOHANG);
    } while (waited < 0 && errno == EINTR);
    if (waited == process) {
        *reaped = true;
        return 0;
    }
    return waited < 0 ? -1 : 0;
}

static int terminate_and_reap_process(
    pid_t process,
    int *wait_status,
    bool *reaped
)
{
    pid_t waited;

    if (*reaped) {
        return 0;
    }
    if (kill(-process, SIGKILL) != 0 && errno != ESRCH) {
        if (kill(process, SIGKILL) != 0 && errno != ESRCH) {
            return -1;
        }
    } else {
        (void)kill(process, SIGKILL);
    }
    do {
        waited = waitpid(process, wait_status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != process) {
        return -1;
    }
    *reaped = true;
    return 0;
}

static int monitor_process(
    pid_t process,
    int signal_descriptor,
    ProcessStream *standard_output,
    ProcessStream *standard_error,
    CaptureBuffer *stdout_buffer,
    CaptureBuffer *stderr_buffer,
    int *wait_status,
    int *forwarded_signal,
    const SolarProgressObserver *observer,
    uint64_t started_ns
)
{
    bool reaped = false;
    int saved_errno = 0;

    while (!reaped || standard_output->parent_read >= 0 ||
           standard_error->parent_read >= 0) {
        struct pollfd descriptors[3];
        int poll_result;

        if (reap_process_nonblocking(
                process, wait_status, &reaped
            ) != 0) {
            saved_errno = errno;
            goto failure;
        }
        if (reaped && standard_output->parent_read < 0 &&
            standard_error->parent_read < 0) {
            break;
        }

        descriptors[0].fd = signal_descriptor;
        descriptors[0].events = POLLIN;
        descriptors[0].revents = 0;
        descriptors[1].fd = standard_output->parent_read;
        descriptors[1].events = POLLIN | POLLHUP;
        descriptors[1].revents = 0;
        descriptors[2].fd = standard_error->parent_read;
        descriptors[2].events = POLLIN | POLLHUP;
        descriptors[2].revents = 0;

        do {
            /* A finite wait also observes child exit when output uses files. */
            poll_result = poll(descriptors, 3U, 50);
        } while (poll_result < 0 && errno == EINTR);
        if (poll_result < 0) {
            saved_errno = errno;
            goto failure;
        }
        if (started_ns != 0U) {
            uint64_t now = monotonic_nanoseconds();

            solar_progress_activity(
                observer, now >= started_ns ? now - started_ns : 0U
            );
        }
        if ((descriptors[0].revents & POLLNVAL) != 0 ||
            (descriptors[1].revents & POLLNVAL) != 0 ||
            (descriptors[2].revents & POLLNVAL) != 0) {
            saved_errno = EBADF;
            goto failure;
        }
        if ((descriptors[0].revents & (POLLIN | POLLERR)) != 0 &&
            consume_forwarded_signals(
                signal_descriptor, process, forwarded_signal
            ) != 0) {
            saved_errno = errno;
            goto failure;
        }
        if (descriptors[1].fd >= 0 &&
            (descriptors[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            if (read_stream_descriptor(
                    standard_output, stdout_buffer, observer, false
                ) != 0) {
                saved_errno = errno;
                goto failure;
            }
        }
        if (descriptors[2].fd >= 0 &&
            (descriptors[2].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            if (read_stream_descriptor(
                    standard_error, stderr_buffer, observer, true
                ) != 0) {
                saved_errno = errno;
                goto failure;
            }
        }
    }

    if (consume_forwarded_signals(
            signal_descriptor, process, forwarded_signal
        ) != 0) {
        saved_errno = errno;
    }
    if (saved_errno != 0) {
        errno = saved_errno;
        return -1;
    }
    return 0;

failure:
    if (standard_output->parent_read >= 0) {
        (void)close(standard_output->parent_read);
        standard_output->parent_read = -1;
    }
    if (standard_error->parent_read >= 0) {
        (void)close(standard_error->parent_read);
        standard_error->parent_read = -1;
    }
    if (terminate_and_reap_process(process, wait_status, &reaped) != 0 &&
        saved_errno == 0) {
        saved_errno = errno;
    }
    errno = saved_errno == 0 ? EIO : saved_errno;
    return -1;
}

static SolarResult result_from_wait_status(
    const SolarProcessSpec *specification,
    int wait_status,
    int exec_error,
    int forwarded_signal,
    SolarProcessResult *process_result
)
{
    if (forwarded_signal != 0) {
        process_result->outcome = SOLAR_PROCESS_SIGNALED;
        process_result->term_signal = forwarded_signal;
        return runner_error(
            SOLAR_STATUS_PROCESS_FAILED,
            "the external process was stopped after Solar received an interrupt",
            "%s interrupted by signal %d",
            specification->executable,
            forwarded_signal
        );
    }
    if (exec_error != 0) {
        process_result->outcome = SOLAR_PROCESS_EXEC_FAILED;
        process_result->exec_errno = exec_error;
        return runner_error(
            exec_error == ENOENT ? SOLAR_STATUS_TOOL_MISSING : SOLAR_STATUS_PROCESS_FAILED,
            exec_error == ENOENT
                ? "install the tool and ensure it is available on PATH"
                : "check executable permissions and the working directory",
            "cannot execute %s: %s",
            specification->executable,
            strerror(exec_error)
        );
    }
    if (WIFSIGNALED(wait_status)) {
        process_result->outcome = SOLAR_PROCESS_SIGNALED;
        process_result->term_signal = WTERMSIG(wait_status);
        return runner_error(
            SOLAR_STATUS_PROCESS_FAILED,
            "inspect the tool log and retry the operation",
            "%s terminated by signal %d",
            specification->executable,
            process_result->term_signal
        );
    }
    if (!WIFEXITED(wait_status)) {
        return runner_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "report this unexpected process state",
            "%s finished with an unknown wait status",
            specification->executable
        );
    }

    process_result->outcome = SOLAR_PROCESS_EXITED;
    process_result->exit_code = WEXITSTATUS(wait_status);
    if (process_result->exit_code != 0) {
        return runner_error(
            SOLAR_STATUS_PROCESS_FAILED,
            specification->stderr_log_path == NULL
                ? "inspect the captured tool error output"
                : "inspect the preserved stderr log",
            "%s exited with status %d",
            specification->executable,
            process_result->exit_code
        );
    }
    return solar_result_ok();
}

SolarResult solar_runner_run(
    const SolarProcessSpec *specification,
    SolarProcessResult *process_result
)
{
    ProcessStream standard_output = {-1, -1, -1, false};
    ProcessStream standard_error = {-1, -1, -1, false};
    CaptureBuffer stdout_capture = {NULL, 0U, false, false};
    CaptureBuffer stderr_capture = {NULL, 0U, false, false};
    SignalMonitor signal_monitor;
    int exec_pipe[2] = {-1, -1};
    int exec_error = 0;
    int exec_read_result;
    int forwarded_signal = 0;
    int wait_status = 0;
    pid_t process = -1;
    uint64_t started_ns = 0U;
    SolarResult result;

    signal_monitor.descriptor = -1;
    signal_monitor.mask_changed = false;

    if (process_result != NULL) {
        solar_process_result_init(process_result);
    }
    if (specification == NULL || process_result == NULL ||
        specification->executable == NULL || specification->argv == NULL ||
        specification->argv[0] == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot run a process without an executable, argv, and result storage",
            "provide a null-terminated argument vector whose first item is the executable"
        );
    }

    started_ns = monotonic_nanoseconds();

    result = validate_environment_additions(specification);
    if (result.status != SOLAR_STATUS_OK) {
        return result;
    }

    result = prepare_stream(
        specification->stdout_log_path, "stdout",
        specification->progress_observer != NULL &&
            specification->progress_observer->tool_output != NULL,
        &standard_output
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = prepare_stream(
        specification->stderr_log_path, "stderr",
        specification->progress_observer != NULL &&
            specification->progress_observer->tool_output != NULL,
        &standard_error
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (prepare_exec_pipe(exec_pipe) != 0) {
        result = runner_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "reduce open files and try again",
            "cannot create exec status pipe: %s",
            strerror(errno)
        );
        goto cleanup;
    }
    if (prepare_signal_monitor(&signal_monitor) != 0) {
        result = runner_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "retry the process; report the issue if signal monitoring remains unavailable",
            "cannot prepare process signal monitoring: %s",
            strerror(errno)
        );
        goto cleanup;
    }

    process = fork();
    if (process < 0) {
        result = runner_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "reduce running processes and try again",
            "cannot fork %s: %s",
            specification->executable,
            strerror(errno)
        );
        goto cleanup;
    }
    if (process == 0) {
        run_child(
            specification, &standard_output, &standard_error, exec_pipe,
            &signal_monitor
        );
    }

    /* This closes the small post-fork race before the child calls setpgid(). */
    /* The child performs and validates the same operation before exec. */
    (void)setpgid(process, process);

    (void)close(exec_pipe[1]);
    exec_pipe[1] = -1;
    (void)close(standard_output.child_write);
    standard_output.child_write = -1;
    (void)close(standard_error.child_write);
    standard_error.child_write = -1;

    exec_read_result = read_exec_error(exec_pipe[0], &exec_error);
    (void)close(exec_pipe[0]);
    exec_pipe[0] = -1;
    if (monitor_process(
            process,
            signal_monitor.descriptor,
            &standard_output,
            &standard_error,
            &stdout_capture,
            &stderr_capture,
            &wait_status,
            &forwarded_signal,
            specification->progress_observer,
            started_ns
        ) != 0) {
        result = runner_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "retry the process and report the issue if it persists",
            "failed while monitoring %s: %s",
            specification->executable,
            strerror(errno)
        );
    } else if (exec_read_result < 0) {
        result = runner_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "retry the process; report the issue if it persists",
            "failed to receive exec status for %s",
            specification->executable
        );
    } else {
        result = solar_result_ok();
    }

    if (result.status == SOLAR_STATUS_OK) {
        result = result_from_wait_status(
            specification, wait_status, exec_error, forwarded_signal,
            process_result
        );
    }

    process_result->stdout_text = stdout_capture.text;
    process_result->stderr_text = stderr_capture.text;
    process_result->stdout_truncated = stdout_capture.truncated;
    process_result->stderr_truncated = stderr_capture.truncated;
    stdout_capture.text = NULL;
    stderr_capture.text = NULL;
    if ((stdout_capture.allocation_failed || stderr_capture.allocation_failed) &&
        result.status == SOLAR_STATUS_OK) {
        result = solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not retain captured process output",
            "free memory and try again"
        );
    }

cleanup:
    close_stream(&standard_output);
    close_stream(&standard_error);
    if (exec_pipe[0] >= 0) (void)close(exec_pipe[0]);
    if (exec_pipe[1] >= 0) (void)close(exec_pipe[1]);
    if (close_signal_monitor(&signal_monitor) != 0) {
        result = runner_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "report this failure because the caller's signal mask may be altered",
            "cannot restore the process signal mask: %s",
            strerror(errno)
        );
    }
    free(stdout_capture.text);
    free(stderr_capture.text);
    if (started_ns != 0U) {
        uint64_t finished_ns = monotonic_nanoseconds();

        if (finished_ns >= started_ns) {
            process_result->duration_ns = finished_ns - started_ns;
        }
    }
    return result;
}

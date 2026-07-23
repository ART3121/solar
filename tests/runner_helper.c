#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int run_blocking_helper(const char *parent_text)
{
    char *end = NULL;
    long parent_value;

    errno = 0;
    parent_value = strtol(parent_text, &end, 10);
    if (errno != 0 || end == parent_text || *end != '\0' ||
        parent_value <= 0) {
        return 65;
    }

    (void)printf("%ld\n", (long)getpid());
    if (fflush(stdout) != 0) {
        return 66;
    }
    if (kill((pid_t)parent_value, SIGINT) != 0) {
        return 67;
    }
    for (;;) {
        (void)pause();
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        (void)fprintf(
            stderr,
            "usage: runner_helper <success|fail|signal|block|environment|slow>\n"
        );
        return 64;
    }

    if (strcmp(argv[1], "success") == 0) {
        (void)fputs("helper stdout\n", stdout);
        (void)fputs("helper stderr\n", stderr);
        return 0;
    }
    if (strcmp(argv[1], "fail") == 0) {
        (void)fputs("failure stdout\n", stdout);
        (void)fputs("failure stderr\n", stderr);
        return 23;
    }
    if (strcmp(argv[1], "signal") == 0) {
        return raise(SIGTERM) == 0 ? 70 : 71;
    }
    if (strcmp(argv[1], "block") == 0 && argc == 3) {
        return run_blocking_helper(argv[2]);
    }
    if (strcmp(argv[1], "environment") == 0 && argc == 3) {
        const char *value = getenv(argv[2]);

        if (value == NULL) {
            return 68;
        }
        (void)puts(value);
        return 0;
    }
    if (strcmp(argv[1], "slow") == 0) {
        const struct timespec delay = {0, 350000000L};

        (void)fputs("slow start\n", stdout);
        (void)fflush(stdout);
        if (nanosleep(&delay, NULL) != 0) return 69;
        (void)fputs("slow done\n", stdout);
        return 0;
    }
    if (strcmp(argv[1], "large") == 0) {
        char chunk[1024];
        int index;

        (void)memset(chunk, 'O', sizeof(chunk));
        for (index = 0; index < 2048; index++) {
            if (fwrite(chunk, 1U, sizeof(chunk), stdout) != sizeof(chunk)) {
                return 72;
            }
        }
        (void)memset(chunk, 'E', sizeof(chunk));
        for (index = 0; index < 2048; index++) {
            if (fwrite(chunk, 1U, sizeof(chunk), stderr) != sizeof(chunk)) {
                return 73;
            }
        }
        return 0;
    }

    (void)fprintf(stderr, "unknown runner helper mode: %s\n", argv[1]);
    return 64;
}

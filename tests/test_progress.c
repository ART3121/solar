#define _POSIX_C_SOURCE 200809L

#include "../src/cli/progress.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fail(const char *name, const char *message)
{
    (void)fprintf(stderr, "%s: %s\n", name, message);
    return 1;
}

static char *read_stream(FILE *stream)
{
    long length;
    char *text;

    if (fflush(stream) != 0 || fseek(stream, 0L, SEEK_END) != 0) return NULL;
    length = ftell(stream);
    if (length < 0 || fseek(stream, 0L, SEEK_SET) != 0) return NULL;
    text = malloc((size_t)length + 1U);
    if (text == NULL) return NULL;
    if (fread(text, 1U, (size_t)length, stream) != (size_t)length) {
        free(text);
        return NULL;
    }
    text[length] = '\0';
    return text;
}

static int test_percentages_and_time(void)
{
    const char *name = "progress percentage and time";
    char time_text[32];

    if (solar_cli_progress_percentage(SOLAR_PROGRESS_LOAD, false, 0U, 1U) != 5U ||
        solar_cli_progress_percentage(SOLAR_PROGRESS_VALIDATE, false, 0U, 1U) != 10U ||
        solar_cli_progress_percentage(SOLAR_PROGRESS_RESOLVE, false, 0U, 1U) != 20U ||
        solar_cli_progress_percentage(SOLAR_PROGRESS_SIMULATION_COMPILE, false, 0U, 1U) != 40U ||
        solar_cli_progress_percentage(SOLAR_PROGRESS_SIMULATION_RUN, false, 0U, 1U) != 85U ||
        solar_cli_progress_percentage(SOLAR_PROGRESS_PUBLISH, false, 0U, 1U) != 95U ||
        solar_cli_progress_percentage(SOLAR_PROGRESS_FINALIZE, false, 0U, 1U) != 100U ||
        solar_cli_progress_percentage(SOLAR_PROGRESS_COMPLETE, false, 0U, 1U) != 100U ||
        solar_cli_progress_percentage(SOLAR_PROGRESS_GENERATE, true, 0U, 1U) != 30U ||
        solar_cli_progress_percentage(SOLAR_PROGRESS_SIMULATION_COMPILE, true, 0U, 1U) != 50U ||
        solar_cli_progress_percentage(SOLAR_PROGRESS_SIMULATION_RUN, true, 0U, 1U) != 85U) {
        return fail(name, "stage percentages differ from the public schedule");
    }
    solar_cli_progress_format_time(
        UINT64_C(61420000000), time_text, sizeof(time_text)
    );
    return strcmp(time_text, "01:01.42") == 0
        ? 0 : fail(name, "elapsed time was formatted incorrectly");
}

static int test_linear_and_disabled_output(void)
{
    const char *name = "progress linear and disabled output";
    SolarCliProgress progress;
    const SolarProgressObserver *observer;
    FILE *stream = tmpfile();
    char *text;
    int failed = 0;

    if (stream == NULL) return fail(name, "tmpfile failed");
    solar_cli_progress_init(&progress, stream, stream, true, false, false, 0);
    observer = solar_cli_progress_observer(&progress);
    solar_progress_stage(observer, SOLAR_PROGRESS_LOAD, "Loading", NULL);
    solar_progress_stage(observer, SOLAR_PROGRESS_RESOLVE, "Resolving", NULL);
    solar_progress_stage(observer, SOLAR_PROGRESS_COMPLETE, "Done", NULL);
    text = read_stream(stream);
    if (text == NULL || strstr(text, "[sim] loading project") == NULL ||
        strstr(text, "[sim] resolving sources") == NULL ||
        strstr(text, "[sim] completed in") == NULL || strchr(text, '\r') != NULL) {
        failed = fail(name, "non-TTY output was not stable linear text");
    }
    free(text);
    (void)fclose(stream);

    stream = tmpfile();
    if (stream == NULL) return failed + fail(name, "second tmpfile failed");
    solar_cli_progress_init(&progress, stream, stream, false, false, false, 1);
    if (solar_cli_progress_observer(&progress) != NULL) {
        failed += fail(name, "--no-progress left an active observer");
    }
    text = read_stream(stream);
    if (text == NULL || text[0] != '\0') {
        failed += fail(name, "--no-progress emitted output");
    }
    free(text);
    (void)fclose(stream);
    return failed;
}

static int test_tty_activity(void)
{
    const char *name = "progress TTY activity";
    struct timespec delay = {0, 100000000L};
    SolarCliProgress progress;
    const SolarProgressObserver *observer;
    FILE *stream = tmpfile();
    char *text;
    int failed = 0;

    if (stream == NULL) return fail(name, "tmpfile failed");
    solar_cli_progress_init(&progress, stream, stream, true, false, false, 1);
    observer = solar_cli_progress_observer(&progress);
    solar_progress_stage(
        observer, SOLAR_PROGRESS_SIMULATION_RUN, "Running simulation", "VVP"
    );
    (void)nanosleep(&delay, NULL);
    solar_progress_activity(observer, UINT64_C(100000000));
    solar_progress_stage(
        observer, SOLAR_PROGRESS_COMPLETE, "Simulation completed", "VVP"
    );
    text = read_stream(stream);
    if (text == NULL || strstr(text, "\r\033[2K[") == NULL ||
        strstr(text, " 85%") == NULL || strstr(text, "VVP") == NULL ||
        strstr(text, "100%") == NULL) {
        failed = fail(name, "TTY bar did not redraw with activity and backend");
    }
    free(text);
    (void)fclose(stream);
    return failed;
}

static int test_verbose_and_failures(void)
{
    const char *name = "progress verbose and failures";
    const SolarProgressStage stages[] = {
        SOLAR_PROGRESS_LOAD, SOLAR_PROGRESS_VALIDATE, SOLAR_PROGRESS_RESOLVE,
        SOLAR_PROGRESS_GENERATE, SOLAR_PROGRESS_SIMULATION_COMPILE,
        SOLAR_PROGRESS_SIMULATION_RUN, SOLAR_PROGRESS_PUBLISH,
        SOLAR_PROGRESS_FINALIZE
    };
    size_t index;
    int failed = 0;

    for (index = 0U; index < sizeof(stages) / sizeof(stages[0]); index++) {
        SolarCliProgress progress;
        const SolarProgressObserver *observer;
        FILE *stream = tmpfile();
        char *text;

        if (stream == NULL) return failed + fail(name, "tmpfile failed");
        solar_cli_progress_init(&progress, stream, stream, true, true, false, 1);
        observer = solar_cli_progress_observer(&progress);
        solar_progress_stage(observer, stages[index], "selected stage", "tool");
        solar_progress_tool_output(observer, false, "tool output\n", 12U);
        solar_progress_stage(observer, SOLAR_PROGRESS_FAILED, "failed", "tool");
        text = read_stream(stream);
        if (text == NULL || strstr(text, "tool output") == NULL ||
            strstr(text, "failed during selected stage") == NULL ||
            strchr(text, '\r') != NULL) {
            failed += fail(name, "verbose/failure output was incomplete");
        }
        free(text);
        (void)fclose(stream);
    }
    return failed;
}

int main(void)
{
    int failures = 0;

    failures += test_percentages_and_time();
    failures += test_linear_and_disabled_output();
    failures += test_tty_activity();
    failures += test_verbose_and_failures();
    return failures == 0 ? 0 : 1;
}

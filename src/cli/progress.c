#define _POSIX_C_SOURCE 200809L

#include "progress.h"

#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PROGRESS_BAR_WIDTH 30U
#define ACTIVITY_INTERVAL_NS UINT64_C(80000000)

static uint64_t monotonic_nanoseconds(void)
{
    struct timespec value;

    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0U;
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
        (uint64_t)value.tv_nsec;
}

void solar_cli_progress_format_time(
    uint64_t nanoseconds,
    char *buffer,
    size_t capacity
)
{
    uint64_t centiseconds = nanoseconds / UINT64_C(10000000);
    uint64_t minutes = centiseconds / UINT64_C(6000);
    uint64_t seconds = (centiseconds / UINT64_C(100)) % UINT64_C(60);
    uint64_t fraction = centiseconds % UINT64_C(100);

    if (buffer == NULL || capacity == 0U) return;
    (void)snprintf(
        buffer, capacity, "%02" PRIu64 ":%02" PRIu64 ".%02" PRIu64,
        minutes, seconds, fraction
    );
}

static unsigned base_percentage(
    SolarProgressStage stage,
    bool yanc_project
)
{
    switch (stage) {
    case SOLAR_PROGRESS_LOAD: return 5U;
    case SOLAR_PROGRESS_VALIDATE: return 10U;
    case SOLAR_PROGRESS_RESOLVE: return yanc_project ? 15U : 20U;
    case SOLAR_PROGRESS_GENERATE: return yanc_project ? 30U : 20U;
    case SOLAR_PROGRESS_SIMULATION_COMPILE: return yanc_project ? 50U : 40U;
    case SOLAR_PROGRESS_SIMULATION_RUN: return 85U;
    case SOLAR_PROGRESS_PUBLISH: return 95U;
    case SOLAR_PROGRESS_FINALIZE: return 100U;
    case SOLAR_PROGRESS_COMPLETE: return 100U;
    case SOLAR_PROGRESS_FAILED: return 0U;
    }
    return 0U;
}

unsigned solar_cli_progress_percentage(
    SolarProgressStage stage,
    bool yanc_project,
    size_t item_index,
    size_t item_count
)
{
    unsigned start = yanc_project ? 50U : 40U;
    unsigned local;
    unsigned span = 95U - start;

    if (stage < SOLAR_PROGRESS_SIMULATION_COMPILE ||
        stage > SOLAR_PROGRESS_PUBLISH || item_count <= 1U) {
        return base_percentage(stage, yanc_project);
    }
    if (item_index >= item_count) item_index = item_count - 1U;
    local = base_percentage(stage, yanc_project) - start;
    return start + (unsigned)((span * item_index + local) / item_count);
}

static const char *linear_stage_text(SolarProgressStage stage)
{
    switch (stage) {
    case SOLAR_PROGRESS_LOAD: return "loading project";
    case SOLAR_PROGRESS_VALIDATE: return "validating configuration";
    case SOLAR_PROGRESS_RESOLVE: return "resolving sources";
    case SOLAR_PROGRESS_GENERATE: return "generating hardware with YANC";
    case SOLAR_PROGRESS_SIMULATION_COMPILE: return "compiling simulation";
    case SOLAR_PROGRESS_SIMULATION_RUN: return "running simulation";
    case SOLAR_PROGRESS_PUBLISH: return "publishing waveform";
    case SOLAR_PROGRESS_FINALIZE: return "finalizing simulation";
    case SOLAR_PROGRESS_COMPLETE: return "completed";
    case SOLAR_PROGRESS_FAILED: return "failed";
    }
    return "working";
}

static void render_tty(SolarCliProgress *progress, uint64_t now)
{
    static const char spinner[] = "/-\\|";
    char stage_time[32];
    char total_time[32];
    unsigned filled;
    unsigned index;

    if (!progress->enabled || !progress->interactive) return;
    solar_cli_progress_format_time(
        now >= progress->stage_started_ns
            ? now - progress->stage_started_ns : 0U,
        stage_time, sizeof(stage_time)
    );
    solar_cli_progress_format_time(
        now >= progress->started_ns ? now - progress->started_ns : 0U,
        total_time, sizeof(total_time)
    );
    filled = progress->percentage * PROGRESS_BAR_WIDTH / 100U;
    (void)fputs("\r\033[2K[", progress->output);
    for (index = 0U; index < PROGRESS_BAR_WIDTH; index++) {
        (void)fputc(index < filled ? '#' : '-', progress->output);
    }
    (void)fprintf(
        progress->output, "] %3u%%  %s step  %s total  %s",
        progress->percentage, stage_time, total_time, progress->label
    );
    if (progress->backend[0] != '\0') {
        (void)fprintf(progress->output, "  [%s]", progress->backend);
    }
    if (progress->stage != SOLAR_PROGRESS_COMPLETE &&
        progress->stage != SOLAR_PROGRESS_FAILED) {
        (void)fprintf(
            progress->output, " %c", spinner[progress->spinner_index++ % 4U]
        );
    }
    (void)fflush(progress->output);
    progress->line_active = true;
    progress->last_render_ns = now;
}

static void progress_stage_changed(
    void *user_data,
    SolarProgressStage stage,
    const char *label,
    const char *backend
)
{
    SolarCliProgress *progress = user_data;
    uint64_t now = monotonic_nanoseconds();
    const char *failed_label = progress->label;
    bool same_stage = stage == progress->stage &&
        stage != SOLAR_PROGRESS_LOAD && stage != SOLAR_PROGRESS_FAILED;

    if (stage != SOLAR_PROGRESS_FAILED) {
        (void)snprintf(
            progress->label, sizeof(progress->label), "%s",
            label == NULL ? linear_stage_text(stage) : label
        );
        (void)snprintf(
            progress->backend, sizeof(progress->backend), "%s",
            backend == NULL ? "" : backend
        );
        if (!same_stage) progress->stage_started_ns = now;
    }
    progress->stage = stage;
    if (stage != SOLAR_PROGRESS_FAILED) {
        unsigned percentage = solar_cli_progress_percentage(
            stage, progress->yanc_project,
            progress->item_index, progress->item_count
        );

        if (percentage > progress->percentage) progress->percentage = percentage;
    }
    if (!progress->enabled) return;
    if (progress->interactive) {
        render_tty(progress, now);
        if (stage == SOLAR_PROGRESS_COMPLETE || stage == SOLAR_PROGRESS_FAILED) {
            (void)fputc('\n', progress->output);
            (void)fflush(progress->output);
            progress->line_active = false;
        }
        return;
    }
    if (same_stage) return;
    if (stage == SOLAR_PROGRESS_COMPLETE) {
        (void)fprintf(
            progress->output, "[sim] completed in %.2f s\n",
            (double)(now - progress->started_ns) / 1000000000.0
        );
    } else if (stage == SOLAR_PROGRESS_FAILED) {
        (void)fprintf(
            progress->output, "[sim] failed during %s after %.2f s\n",
            failed_label[0] == '\0' ? "simulation" : failed_label,
            (double)(now - progress->started_ns) / 1000000000.0
        );
    } else {
        if (stage == SOLAR_PROGRESS_SIMULATION_COMPILE && backend != NULL) {
            (void)fprintf(
                progress->output, "[sim] compiling with %s\n", backend
            );
        } else {
            (void)fprintf(
                progress->output, "[sim] %s\n", linear_stage_text(stage)
            );
        }
    }
    (void)fflush(progress->output);
}

static void progress_item_changed(
    void *user_data,
    size_t item_index,
    size_t item_count,
    const char *item_name
)
{
    SolarCliProgress *progress = user_data;

    progress->item_index = item_index;
    progress->item_count = item_count == 0U ? 1U : item_count;
    (void)item_name;
}

static void progress_activity(void *user_data, uint64_t process_elapsed_ns)
{
    SolarCliProgress *progress = user_data;
    uint64_t now = monotonic_nanoseconds();

    (void)process_elapsed_ns;
    if (!progress->interactive || !progress->enabled ||
        now - progress->last_render_ns < ACTIVITY_INTERVAL_NS) {
        return;
    }
    render_tty(progress, now);
}

static void progress_tool_output(
    void *user_data,
    bool standard_error,
    const char *data,
    size_t length
)
{
    SolarCliProgress *progress = user_data;
    FILE *stream;

    if (!progress->verbose || data == NULL || length == 0U) return;
    stream = standard_error ? progress->error_output : progress->output;
    (void)fwrite(data, 1U, length, stream);
    (void)fflush(stream);
}

void solar_cli_progress_init(
    SolarCliProgress *progress,
    FILE *output,
    FILE *error_output,
    bool enabled,
    bool verbose,
    bool yanc_project,
    int tty_override
)
{
    bool terminal;

    if (progress == NULL) return;
    (void)memset(progress, 0, sizeof(*progress));
    progress->output = output == NULL ? stdout : output;
    progress->error_output = error_output == NULL ? stderr : error_output;
    terminal = tty_override >= 0
        ? tty_override != 0
        : isatty(fileno(progress->output)) != 0;
    progress->enabled = enabled;
    /* Tool output is streamed linearly so partial chunks cannot corrupt a bar. */
    progress->interactive = enabled && terminal && !verbose;
    progress->verbose = verbose;
    progress->yanc_project = yanc_project;
    progress->item_count = 1U;
    progress->started_ns = monotonic_nanoseconds();
    progress->stage_started_ns = progress->started_ns;
    progress->observer.stage_changed = progress_stage_changed;
    progress->observer.item_changed = progress_item_changed;
    progress->observer.activity = progress_activity;
    progress->observer.tool_output = verbose ? progress_tool_output : NULL;
    progress->observer.user_data = progress;
}

const SolarProgressObserver *solar_cli_progress_observer(
    SolarCliProgress *progress
)
{
    if (progress == NULL || (!progress->enabled && !progress->verbose)) {
        return NULL;
    }
    return &progress->observer;
}

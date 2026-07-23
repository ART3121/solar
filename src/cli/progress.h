#ifndef SOLAR_CLI_PROGRESS_H
#define SOLAR_CLI_PROGRESS_H

#include "solar/progress.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    SolarProgressObserver observer;
    FILE *output;
    FILE *error_output;
    uint64_t started_ns;
    uint64_t stage_started_ns;
    uint64_t last_render_ns;
    SolarProgressStage stage;
    size_t item_index;
    size_t item_count;
    unsigned percentage;
    unsigned spinner_index;
    char label[96];
    char backend[48];
    bool enabled;
    bool interactive;
    bool verbose;
    bool yanc_project;
    bool line_active;
} SolarCliProgress;

/* tty_override: -1 detects stdout, 0 forces linear, 1 forces TTY rendering. */
void solar_cli_progress_init(
    SolarCliProgress *progress,
    FILE *output,
    FILE *error_output,
    bool enabled,
    bool verbose,
    bool yanc_project,
    int tty_override
);

const SolarProgressObserver *solar_cli_progress_observer(
    SolarCliProgress *progress
);
unsigned solar_cli_progress_percentage(
    SolarProgressStage stage,
    bool yanc_project,
    size_t item_index,
    size_t item_count
);
void solar_cli_progress_format_time(
    uint64_t nanoseconds,
    char *buffer,
    size_t capacity
);

#endif

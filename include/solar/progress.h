#ifndef SOLAR_PROGRESS_H
#define SOLAR_PROGRESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SOLAR_PROGRESS_LOAD = 0,
    SOLAR_PROGRESS_VALIDATE,
    SOLAR_PROGRESS_RESOLVE,
    SOLAR_PROGRESS_GENERATE,
    SOLAR_PROGRESS_SIMULATION_COMPILE,
    SOLAR_PROGRESS_SIMULATION_RUN,
    SOLAR_PROGRESS_PUBLISH,
    SOLAR_PROGRESS_FINALIZE,
    SOLAR_PROGRESS_COMPLETE,
    SOLAR_PROGRESS_FAILED
} SolarProgressStage;

/*
 * The observer and strings are borrowed for the duration of each callback.
 * Callbacks run synchronously on Solar's orchestration thread; they must return
 * promptly and must not call back into the active build.
 */
typedef struct {
    void (*stage_changed)(
        void *user_data,
        SolarProgressStage stage,
        const char *label,
        const char *backend
    );
    void (*item_changed)(
        void *user_data,
        size_t item_index,
        size_t item_count,
        const char *item_name
    );
    void (*activity)(void *user_data, uint64_t process_elapsed_ns);
    void (*tool_output)(
        void *user_data,
        bool standard_error,
        const char *data,
        size_t length
    );
    void *user_data;
} SolarProgressObserver;

void solar_progress_stage(
    const SolarProgressObserver *observer,
    SolarProgressStage stage,
    const char *label,
    const char *backend
);
void solar_progress_item(
    const SolarProgressObserver *observer,
    size_t item_index,
    size_t item_count,
    const char *item_name
);
void solar_progress_activity(
    const SolarProgressObserver *observer,
    uint64_t process_elapsed_ns
);
void solar_progress_tool_output(
    const SolarProgressObserver *observer,
    bool standard_error,
    const char *data,
    size_t length
);

#endif

#include "solar/progress.h"

void solar_progress_stage(
    const SolarProgressObserver *observer,
    SolarProgressStage stage,
    const char *label,
    const char *backend
)
{
    if (observer != NULL && observer->stage_changed != NULL) {
        observer->stage_changed(observer->user_data, stage, label, backend);
    }
}

void solar_progress_item(
    const SolarProgressObserver *observer,
    size_t item_index,
    size_t item_count,
    const char *item_name
)
{
    if (observer != NULL && observer->item_changed != NULL) {
        observer->item_changed(
            observer->user_data, item_index, item_count, item_name
        );
    }
}

void solar_progress_activity(
    const SolarProgressObserver *observer,
    uint64_t process_elapsed_ns
)
{
    if (observer != NULL && observer->activity != NULL) {
        observer->activity(observer->user_data, process_elapsed_ns);
    }
}

void solar_progress_tool_output(
    const SolarProgressObserver *observer,
    bool standard_error,
    const char *data,
    size_t length
)
{
    if (observer != NULL && observer->tool_output != NULL &&
        data != NULL && length > 0U) {
        observer->tool_output(
            observer->user_data, standard_error, data, length
        );
    }
}

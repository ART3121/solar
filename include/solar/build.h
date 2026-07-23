#ifndef SOLAR_BUILD_H
#define SOLAR_BUILD_H

#include "solar/compiler.h"
#include "solar/filesystem.h"
#include "solar/project.h"
#include "solar/progress.h"
#include "solar/result.h"
#include "solar/rtl.h"
#include "solar/synthesis.h"
#include "solar/test.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define SOLAR_BUILD_MAX_STEPS 12U

typedef struct {
    char name[24];
    SolarResult result;
    uint64_t duration_ns;
    uint64_t duration_ms;
    bool skipped;
} SolarBuildStepResult;

/*
 * One command or pipeline owns one context. The context owns the loaded
 * project and every result below until solar_build_context_free(). Keeping the
 * compiler result here lets later test and synthesis stages reuse one YANC run.
 */
typedef struct {
    SolarProject project;
    char *operation;
    char *requested_profile;
    char *profile_name;
    SolarCompilerResult compiler_result;
    SolarRtlBuildResult rtl_result;
    SolarTestResult test_result;
    SolarTestSummary test_summary;
    SolarSynthesisResult synthesis_result;
    SolarBuildStepResult steps[SOLAR_BUILD_MAX_STEPS];
    size_t step_count;
    SolarResult result;
    uint64_t started_ns;
    uint64_t duration_ns;
    uint64_t duration_ms;
    time_t started_at;
    time_t finished_at;
    uint64_t load_duration_ns;
    uint64_t validation_duration_ns;
    SolarProjectLock project_lock;
    bool lock_attempted;
    bool owns_project_lock;
    /* Borrowed for the lifetime of the active command. */
    const SolarProgressObserver *progress_observer;
    bool dry_run;
    bool generated;
    bool rtl_ready;
    bool has_test_result;
    bool has_test_summary;
    bool has_synthesis_result;
} SolarBuildContext;

void solar_build_context_init(SolarBuildContext *context);
void solar_build_context_free(SolarBuildContext *context);
void solar_build_context_set_progress(
    SolarBuildContext *context,
    const SolarProgressObserver *progress_observer
);

SolarResult solar_build_context_load(
    SolarBuildContext *context,
    const char *start_path,
    const char *operation,
    const char *profile_name,
    bool dry_run
);
SolarResult solar_build_context_check(SolarBuildContext *context);
SolarResult solar_build_context_rtl(SolarBuildContext *context);
SolarResult solar_build_context_test(
    SolarBuildContext *context,
    const char *test_name,
    bool run_all,
    bool simulation_operation
);
SolarResult solar_build_context_test_sequence(
    SolarBuildContext *context,
    bool stop_on_failure
);
SolarResult solar_build_context_synthesize(SolarBuildContext *context);

void solar_build_context_finish(
    SolarBuildContext *context,
    SolarResult result
);
SolarResult solar_build_report_write(const SolarBuildContext *context);
SolarResult solar_build_report_read(
    const char *start_path,
    char **report_text_out
);

#endif

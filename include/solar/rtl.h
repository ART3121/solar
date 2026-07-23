#ifndef SOLAR_RTL_H
#define SOLAR_RTL_H

#include "solar/config.h"
#include "solar/project.h"
#include "solar/progress.h"
#include "solar/result.h"

#include <stdint.h>

typedef struct {
    char *backend;
    char *project_root;
    char *working_directory;
    char *top;
    SolarStringList rtl_sources;
    SolarStringList include_dirs;
    SolarStringList defines;
    char *stdout_log;
    char *stderr_log;
    /* Borrowed while the request is executed. */
    const SolarProgressObserver *progress_observer;
} SolarRtlBuildRequest;

typedef struct {
    char *backend;
    char *profile_name;
    char *top;
    char *stdout_log;
    char *stderr_log;
    uint64_t duration_ns;
    SolarResult result;
} SolarRtlBuildResult;

void solar_rtl_build_request_init(SolarRtlBuildRequest *request);
void solar_rtl_build_request_free(SolarRtlBuildRequest *request);
void solar_rtl_build_result_init(SolarRtlBuildResult *result);
void solar_rtl_build_result_free(SolarRtlBuildResult *result);

SolarResult solar_rtl_build(
    const SolarProject *project,
    const char *profile_name,
    SolarRtlBuildResult *build_result
);
SolarResult solar_rtl_build_with_progress(
    const SolarProject *project,
    const char *profile_name,
    const SolarProgressObserver *progress_observer,
    SolarRtlBuildResult *build_result
);

#endif

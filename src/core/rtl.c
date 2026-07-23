#include "solar/rtl.h"

#include "solar/backend.h"
#include "solar/filesystem.h"

#include <stdlib.h>
#include <string.h>

static SolarResult copy_string(char **output, const char *value)
{
    if (value == NULL) return solar_result_ok();
    *output = strdup(value);
    if (*output == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate RTL build data",
            "free memory and try again"
        );
    }
    return solar_result_ok();
}

void solar_rtl_build_request_init(SolarRtlBuildRequest *request)
{
    if (request == NULL) return;
    (void)memset(request, 0, sizeof(*request));
    solar_string_list_init(&request->rtl_sources);
    solar_string_list_init(&request->include_dirs);
    solar_string_list_init(&request->defines);
}

void solar_rtl_build_request_free(SolarRtlBuildRequest *request)
{
    if (request == NULL) return;
    free(request->backend);
    free(request->project_root);
    free(request->working_directory);
    free(request->top);
    solar_string_list_free(&request->rtl_sources);
    solar_string_list_free(&request->include_dirs);
    solar_string_list_free(&request->defines);
    free(request->stdout_log);
    free(request->stderr_log);
    solar_rtl_build_request_init(request);
}

void solar_rtl_build_result_init(SolarRtlBuildResult *result)
{
    if (result == NULL) return;
    (void)memset(result, 0, sizeof(*result));
    result->result = solar_result_ok();
}

void solar_rtl_build_result_free(SolarRtlBuildResult *result)
{
    if (result == NULL) return;
    free(result->backend);
    free(result->profile_name);
    free(result->top);
    free(result->stdout_log);
    free(result->stderr_log);
    solar_rtl_build_result_init(result);
}

static SolarResult append_resolved(
    const SolarProject *project,
    const SolarStringList *source,
    SolarStringList *destination
)
{
    size_t index;

    for (index = 0U; index < source->count; index++) {
        char *path = NULL;
        SolarResult result = solar_project_resolve_path(
            project, source->items[index], &path
        );

        if (result.status == SOLAR_STATUS_OK) {
            result = solar_string_list_append_copy(destination, path);
        }
        free(path);
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    return solar_result_ok();
}

static SolarResult append_copies(
    const SolarStringList *source,
    SolarStringList *destination
)
{
    size_t index;

    for (index = 0U; index < source->count; index++) {
        SolarResult result = solar_string_list_append_copy(
            destination, source->items[index]
        );

        if (result.status != SOLAR_STATUS_OK) return result;
    }
    return solar_result_ok();
}

static SolarResult prepare_request(
    const SolarProject *project,
    const SolarProfile *profile,
    SolarRtlBuildRequest *request
)
{
    SolarEffectiveConfig effective;
    SolarResult result;

    solar_effective_config_init(&effective);
    result = copy_string(&request->backend, project->config.simulation.backend);
    if (result.status == SOLAR_STATUS_OK) {
        result = copy_string(&request->project_root, project->root);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = copy_string(&request->top, project->config.synthesis.top);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = append_resolved(
            project, &project->config.sources.rtl, &request->rtl_sources
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_config_build_effective(
            &project->config, profile, NULL, &effective
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = append_resolved(
            project, &effective.include_dirs, &request->include_dirs
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = append_copies(&effective.defines, &request->defines);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_filesystem_prepare_generated_directory(
            project->root, ".solar/tmp/rtl"
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_filesystem_prepare_generated_directory(
            project->root, ".solar/logs/rtl"
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_filesystem_join(
            project->root, ".solar/tmp/rtl", &request->working_directory
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_filesystem_join(
            project->root, ".solar/logs/rtl/elaborate.stdout.log",
            &request->stdout_log
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_filesystem_join(
            project->root, ".solar/logs/rtl/elaborate.stderr.log",
            &request->stderr_log
        );
    }
    solar_effective_config_free(&effective);
    return result;
}

static SolarResult set_result(
    const SolarRtlBuildRequest *request,
    const SolarProfile *profile,
    SolarRtlBuildResult *build_result
)
{
    SolarResult result = copy_string(&build_result->backend, request->backend);

    if (result.status == SOLAR_STATUS_OK) {
        result = copy_string(
            &build_result->profile_name, profile == NULL ? NULL : profile->name
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = copy_string(&build_result->top, request->top);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = copy_string(&build_result->stdout_log, request->stdout_log);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = copy_string(&build_result->stderr_log, request->stderr_log);
    }
    return result;
}

SolarResult solar_rtl_build_with_progress(
    const SolarProject *project,
    const char *profile_name,
    const SolarProgressObserver *progress_observer,
    SolarRtlBuildResult *build_result
)
{
    const SolarProfile *profile = NULL;
    SolarRtlBuildRequest request;
    SolarResult result;

    solar_rtl_build_request_init(&request);
    if (build_result != NULL) solar_rtl_build_result_init(build_result);
    if (project == NULL || build_result == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot build RTL without a project and result storage",
            "load a Verilog project before building RTL"
        );
    }
    result = solar_project_validate(project);
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_config_select_profile(
            &project->config, profile_name, &profile
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = prepare_request(project, profile, &request);
        request.progress_observer = progress_observer;
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = set_result(&request, profile, build_result);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_backend_elaborate_request(
            &request, &build_result->duration_ns
        );
    }
    build_result->result = result;
    solar_rtl_build_request_free(&request);
    return result;
}

SolarResult solar_rtl_build(
    const SolarProject *project,
    const char *profile_name,
    SolarRtlBuildResult *build_result
)
{
    return solar_rtl_build_with_progress(
        project, profile_name, NULL, build_result
    );
}

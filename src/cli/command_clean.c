#include "commands.h"

#include "solar/artifact.h"
#include "solar/filesystem.h"
#include "solar/project.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SolarResult solar_cli_command_clean(
    const char *start_path,
    int argument_count,
    char *const arguments[]
)
{
    enum { CLEAN_DEFAULT, CLEAN_CACHE, CLEAN_ALL } mode = CLEAN_DEFAULT;
    SolarProject project;
    SolarProjectLock project_lock;
    char *project_root = NULL;
    bool internal_removed = false;
    size_t artifact_count = 0U;
    SolarResult result;

    if (argument_count == 1 && strcmp(arguments[0], "--cache") == 0) {
        mode = CLEAN_CACHE;
    } else if (argument_count == 1 && strcmp(arguments[0], "--all") == 0) {
        mode = CLEAN_ALL;
    } else if (argument_count != 0) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "invalid solar clean arguments",
            "use solar clean, solar clean --cache, or solar clean --all"
        );
    }
    solar_project_init(&project);
    solar_project_lock_init(&project_lock);
    result = solar_project_find_root(start_path, &project_root);
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_project_lock_acquire(project_root, &project_lock);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_project_load(project_root, &project);
    }
    if (result.status == SOLAR_STATUS_OK) solar_cli_print_legacy_warning(&project);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (mode != CLEAN_CACHE) {
        result = solar_artifact_remove_registered(
            project.root, &artifact_count
        );
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
    }
    if (mode == CLEAN_ALL) {
        result = solar_filesystem_clean_project(
            project.root, &internal_removed
        );
    } else {
        const char *path = mode == CLEAN_CACHE ? ".solar/cache" : ".solar/tmp";

        result = solar_filesystem_remove_generated_tree(
            project.root, path, &internal_removed
        );
        if (result.status == SOLAR_STATUS_OK && mode == CLEAN_DEFAULT) {
            bool state_removed = false;

            result = solar_filesystem_remove_generated_tree(
                project.root, ".solar/state", &state_removed
            );
            internal_removed = internal_removed || state_removed;
        }
    }
    if (result.status == SOLAR_STATUS_OK) {
        if (artifact_count == 0U && !internal_removed) {
            (void)printf("Nothing to clean.\n");
        } else if (mode == CLEAN_CACHE) {
            (void)printf("Removed Solar cache data.\n");
        } else if (mode == CLEAN_ALL) {
            (void)printf(
                "Removed %zu registered artifact(s) and all internal .solar data.\n",
                artifact_count
            );
        } else {
            (void)printf(
                "Removed %zu registered artifact(s), temporary data, and disposable state.\n",
                artifact_count
            );
        }
    }

cleanup:
    solar_project_lock_release(&project_lock);
    free(project_root);
    solar_project_free(&project);
    return result;
}

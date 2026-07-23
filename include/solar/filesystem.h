#ifndef SOLAR_FILESYSTEM_H
#define SOLAR_FILESYSTEM_H

#include "solar/result.h"

#include <stdbool.h>

typedef struct {
    int descriptor;
} SolarProjectLock;

void solar_project_lock_init(SolarProjectLock *lock);
void solar_project_lock_release(SolarProjectLock *lock);

/*
 * Acquires the non-blocking advisory lock for one mutating project operation.
 * The caller owns the lock until solar_project_lock_release(). Linux releases
 * the directory lock automatically if the process exits unexpectedly.
 */
SolarResult solar_project_lock_acquire(
    const char *project_root,
    SolarProjectLock *lock
);

/* The caller owns the returned path and must free it. */
SolarResult solar_filesystem_join(
    const char *left,
    const char *right,
    char **path_out
);

bool solar_filesystem_is_safe_relative(const char *path);

SolarResult solar_filesystem_prepare_layout(const char *project_root);

/* Creates every component below the project-local .solar root. */
SolarResult solar_filesystem_prepare_generated_directory(
    const char *project_root,
    const char *relative_path
);

/* Removes an existing regular generated file without following symlinks. */
SolarResult solar_filesystem_reset_artifact(
    const char *project_root,
    const char *relative_path
);

/* Creates a generated symlink after traversing its parent without symlinks. */
SolarResult solar_filesystem_create_generated_symlink(
    const char *project_root,
    const char *relative_path,
    const char *absolute_target
);

/* Removes one generated subtree without following symlinks. */
SolarResult solar_filesystem_remove_generated_tree(
    const char *project_root,
    const char *relative_path,
    bool *removed_out
);

/*
 * Promotes staging to a generated final directory. An existing final
 * directory is restored if the promotion fails.
 */
SolarResult solar_filesystem_publish_generated_directory(
    const char *project_root,
    const char *staging_relative_path,
    const char *final_relative_path
);

/* Removes only the project-owned .solar root. */
SolarResult solar_filesystem_clean_project(
    const char *project_root,
    bool *removed_out
);

#endif

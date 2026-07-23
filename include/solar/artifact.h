#ifndef SOLAR_ARTIFACT_H
#define SOLAR_ARTIFACT_H

#include "solar/result.h"

#include <stddef.h>

typedef struct {
    char *type;
    char *path;
    char *stage;
    char *test_name;
    char *backend;
    char *profile;
} SolarArtifactRecord;

typedef struct {
    SolarArtifactRecord *items;
    size_t count;
} SolarArtifactList;

typedef struct {
    const char *temporary_source;
    const char *public_relative_path;
    const char *type;
    const char *stage;
    const char *test_name;
    const char *backend;
    const char *profile;
} SolarArtifactPublication;

void solar_artifact_list_init(SolarArtifactList *list);
void solar_artifact_list_free(SolarArtifactList *list);

/* Missing state is a valid empty list. The caller owns list contents. */
SolarResult solar_artifact_list_load(
    const char *project_root,
    SolarArtifactList *list
);

/* Returns a borrowed record, preferring the most recently registered match. */
const SolarArtifactRecord *solar_artifact_find(
    const SolarArtifactList *list,
    const char *type,
    const char *test_name
);

/*
 * Copies one validated temporary regular file into a public artifact path and
 * registers it. Existing registered output is rolled back on every failure.
 * destination_out is an owned absolute path on success.
 */
SolarResult solar_artifact_publish_file(
    const char *project_root,
    const char *temporary_source,
    const char *public_relative_path,
    const char *type,
    const char *stage,
    const char *test_name,
    const char *backend,
    const char *profile,
    char **destination_out
);

/* Publishes a set with rollback and one atomic registry replacement. */
SolarResult solar_artifact_publish_files(
    const char *project_root,
    const SolarArtifactPublication *publications,
    size_t count
);

/* Removes only registered public regular files. Public directories remain. */
SolarResult solar_artifact_remove_registered(
    const char *project_root,
    size_t *removed_count_out
);

#endif

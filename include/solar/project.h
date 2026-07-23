#ifndef SOLAR_PROJECT_H
#define SOLAR_PROJECT_H

#include "solar/config.h"
#include "solar/discovery.h"

typedef struct SolarProject {
    char *root;
    char *manifest_path;
    bool legacy_build_layout_present;
    SolarConfig config;
    SolarDiscoveryReport discovery;
} SolarProject;

void solar_project_init(SolarProject *project);
void solar_project_free(SolarProject *project);

/* The caller owns root_out and must free it. */
SolarResult solar_project_find_root(const char *start_path, char **root_out);

/* project must be initialized and empty before loading. */
SolarResult solar_project_load(const char *start_path, SolarProject *project);
SolarResult solar_project_validate(const SolarProject *project);

/* The caller owns path_out and must free it. */
SolarResult solar_project_resolve_path(
    const SolarProject *project,
    const char *relative_path,
    char **path_out
);

SolarResult solar_project_initialize(const char *directory);
/* Creates one built-in template without overwriting existing project files. */
SolarResult solar_project_initialize_template(
    const char *directory,
    const char *template_name
);

#endif

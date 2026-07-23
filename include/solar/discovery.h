#ifndef SOLAR_DISCOVERY_H
#define SOLAR_DISCOVERY_H

#include "solar/result.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    size_t rtl_files_found;
    size_t testbench_files_found;
    size_t rtl_files_added;
    size_t test_sources_added;
    size_t tests_added;
    size_t ambiguous_testbench_files;
    bool synthesis_top_inferred;
} SolarDiscoveryReport;

struct SolarProject;

void solar_discovery_report_init(SolarDiscoveryReport *report);

/*
 * Merges conventional rtl/ and tb/ Verilog files into the project's owned
 * in-memory configuration and infers unambiguous synthesis/testbench roots.
 * The manifest on disk is never modified.
 */
SolarResult solar_discovery_apply(
    struct SolarProject *project,
    SolarDiscoveryReport *report
);

#endif

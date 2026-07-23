#ifndef SOLAR_SCAN_H
#define SOLAR_SCAN_H

#include "solar/result.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    size_t rtl_added;
    size_t rtl_removed;
    size_t rtl_total;
    size_t tests_added;
    size_t tests_removed;
    size_t tests_total;
    bool migrated_v1;
    bool changed;
} SolarScanResult;

void solar_scan_result_init(SolarScanResult *result);

/*
 * Discovers conventional Verilog sources and transactionally synchronizes the
 * managed [sources].rtl and [[test]] portions of solar.toml. The manifest is
 * unchanged on every error path.
 */
SolarResult solar_scan_project(const char *start_path, SolarScanResult *result);

#endif

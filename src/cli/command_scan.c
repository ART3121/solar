#include "commands.h"

#include "solar/scan.h"

#include <stdio.h>

SolarResult solar_cli_command_scan(const char *start_path)
{
    SolarScanResult scan;
    SolarResult result;

    solar_scan_result_init(&scan);
    result = solar_scan_project(start_path, &scan);
    if (result.status != SOLAR_STATUS_OK) return result;

    (void)printf(
        "Solar scan\n\n"
        "RTL:   %zu total, %zu added, %zu removed\n"
        "Tests: %zu total, %zu added, %zu removed\n"
        "Manifest: %s%s\n",
        scan.rtl_total, scan.rtl_added, scan.rtl_removed,
        scan.tests_total, scan.tests_added, scan.tests_removed,
        scan.changed ? "updated" : "unchanged",
        scan.migrated_v1 ? " (format 1 -> 2)" : ""
    );
    return solar_result_ok();
}

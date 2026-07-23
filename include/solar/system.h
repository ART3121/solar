#ifndef SOLAR_SYSTEM_H
#define SOLAR_SYSTEM_H

#include "solar/result.h"

#include <stdint.h>

typedef struct {
    char hostname[256];
    char operating_system[256];
    char kernel[256];
    char architecture[128];
    char cpu_model[256];
    long logical_cpus;
    uint64_t memory_total_bytes;
    long page_size_bytes;
} SolarSystemInfo;

/* Collects a non-secret Linux host snapshot without starting another process. */
SolarResult solar_system_info_collect(SolarSystemInfo *information);

#endif

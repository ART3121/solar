#ifndef SOLAR_COMPILER_BACKEND_INTERNAL_H
#define SOLAR_COMPILER_BACKEND_INTERNAL_H

#include "solar/compiler.h"

typedef struct {
    const char *name;
    SolarResult (*compile)(
        const SolarProject *project,
        SolarCompilerResult *compiler_result
    );
} SolarCompilerBackend;

extern const SolarCompilerBackend SOLAR_YANC_COMPILER_BACKEND;

SolarResult solar_compiler_record_stage(
    SolarCompilerResult *compiler_result,
    const char *name,
    const char *executable_path,
    const char *log_path,
    uint64_t duration_ns,
    SolarResult stage_result
);

#endif

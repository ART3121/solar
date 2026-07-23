#ifndef SOLAR_COCOTB_H
#define SOLAR_COCOTB_H

#include "solar/backend.h"
#include "solar/result.h"

/* Runs one cocotb module through the structured cocotb/Verilator adapter. */
SolarResult solar_cocotb_run(
    const SolarSimulationRequest *request,
    const char *test_module_path,
    SolarSimulationFailureKind *failure_kind_out,
    SolarSimulationMetrics *metrics_out
);

#endif

#ifndef SOLAR_WAVEFORM_H
#define SOLAR_WAVEFORM_H

#include "solar/result.h"

#include <stdbool.h>

typedef enum {
    SOLAR_WAVEFORM_VIEWER_GTKWAVE = 0,
    SOLAR_WAVEFORM_VIEWER_SURFER
} SolarWaveformViewer;

const char *solar_waveform_viewer_name(SolarWaveformViewer viewer);

/*
 * Launches the selected external viewer without waiting for its GUI process.
 * interactive should describe the calling frontend; false makes this a no-op
 * for automation. Solar does not own or install the viewer executable.
 */
SolarResult solar_waveform_open_with_viewer(
    const char *waveform_path,
    SolarWaveformViewer viewer,
    bool interactive,
    bool *launched_out
);

/*
 * Compatibility entry point. It retains the pre-Surfer GTKWave behavior.
 */
SolarResult solar_waveform_open(
    const char *waveform_path,
    bool interactive,
    bool *launched_out
);

#endif

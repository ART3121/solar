#include "solar/system.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    SolarSystemInfo information;
    SolarResult result = solar_system_info_collect(&information);

    if (result.status != SOLAR_STATUS_OK || information.hostname[0] == '\0' ||
        information.operating_system[0] == '\0' ||
        information.kernel[0] == '\0' || information.architecture[0] == '\0' ||
        information.cpu_model[0] == '\0' || information.logical_cpus <= 0L ||
        information.memory_total_bytes == 0U ||
        information.page_size_bytes <= 0L) {
        (void)fprintf(stderr, "system information snapshot is incomplete\n");
        return 1;
    }
    return 0;
}

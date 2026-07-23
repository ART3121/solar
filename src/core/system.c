#include "solar/system.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

static void set_unavailable(char *destination, size_t capacity)
{
    (void)snprintf(destination, capacity, "%s", "unavailable");
}

static bool ascii_space(unsigned char value)
{
    return value == ' ' || value == '\t' || value == '\n' ||
        value == '\r' || value == '\f' || value == '\v';
}

static char *trim(char *text)
{
    char *end;

    while (ascii_space((unsigned char)*text)) text++;
    end = text + strlen(text);
    while (end > text && ascii_space((unsigned char)end[-1])) end--;
    *end = '\0';
    return text;
}

static void read_os_name(char *destination, size_t capacity)
{
    FILE *file = fopen("/etc/os-release", "r");
    char line[512];

    if (file == NULL) return;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *value;
        size_t length;

        if (strncmp(line, "PRETTY_NAME=", 12U) != 0) continue;
        value = trim(line + 12U);
        length = strlen(value);
        if (length >= 2U && value[0] == '"' && value[length - 1U] == '"') {
            value[length - 1U] = '\0';
            value++;
        }
        (void)snprintf(destination, capacity, "%s", value);
        break;
    }
    (void)fclose(file);
}

static void read_cpu_model(char *destination, size_t capacity)
{
    FILE *file = fopen("/proc/cpuinfo", "r");
    char line[512];

    if (file == NULL) return;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *separator = strchr(line, ':');
        char *key;

        if (separator == NULL) continue;
        *separator = '\0';
        key = trim(line);
        if (strcmp(key, "model name") != 0 && strcmp(key, "Hardware") != 0) {
            continue;
        }
        (void)snprintf(destination, capacity, "%s", trim(separator + 1));
        break;
    }
    (void)fclose(file);
}

static uint64_t read_memory_total(void)
{
    FILE *file = fopen("/proc/meminfo", "r");
    char line[128];
    char *end = NULL;
    unsigned long long kibibytes;

    if (file == NULL) return 0U;
    if (fgets(line, sizeof(line), file) == NULL ||
        strncmp(line, "MemTotal:", 9U) != 0) {
        (void)fclose(file);
        return 0U;
    }
    (void)fclose(file);
    errno = 0;
    kibibytes = strtoull(line + 9U, &end, 10);
    if (errno != 0 || end == line + 9U ||
        kibibytes > UINT64_MAX / UINT64_C(1024)) {
        return 0U;
    }
    return (uint64_t)kibibytes * UINT64_C(1024);
}

SolarResult solar_system_info_collect(SolarSystemInfo *information)
{
    struct utsname system_name;

    if (information == NULL) return solar_result_error(
        SOLAR_STATUS_INVALID_ARGUMENT,
        "cannot collect system information without output storage",
        "provide a SolarSystemInfo structure"
    );
    (void)memset(information, 0, sizeof(*information));
    set_unavailable(information->hostname, sizeof(information->hostname));
    set_unavailable(
        information->operating_system, sizeof(information->operating_system)
    );
    set_unavailable(information->kernel, sizeof(information->kernel));
    set_unavailable(information->architecture, sizeof(information->architecture));
    set_unavailable(information->cpu_model, sizeof(information->cpu_model));
    if (gethostname(information->hostname, sizeof(information->hostname)) == 0) {
        information->hostname[sizeof(information->hostname) - 1U] = '\0';
    }
    if (uname(&system_name) == 0) {
        (void)snprintf(
            information->kernel, sizeof(information->kernel), "%s %s",
            system_name.sysname, system_name.release
        );
        (void)snprintf(
            information->architecture, sizeof(information->architecture),
            "%s", system_name.machine
        );
    }
    read_os_name(
        information->operating_system, sizeof(information->operating_system)
    );
    read_cpu_model(information->cpu_model, sizeof(information->cpu_model));
    information->logical_cpus = sysconf(_SC_NPROCESSORS_ONLN);
    information->page_size_bytes = sysconf(_SC_PAGESIZE);
    information->memory_total_bytes = read_memory_total();
    return solar_result_ok();
}

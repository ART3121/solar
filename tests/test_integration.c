#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/artifact.h"
#include "solar/filesystem.h"
#include "solar/project.h"
#include "solar/runner.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *join_path(const char *directory, const char *name)
{
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    char *path = malloc(directory_length + name_length + 2U);

    if (path == NULL) {
        return NULL;
    }
    (void)snprintf(
        path,
        directory_length + name_length + 2U,
        "%s/%s",
        directory,
        name
    );
    return path;
}

static int report_failure(const char *test_name, const char *message)
{
    (void)fprintf(stderr, "%s: %s\n", test_name, message);
    return 1;
}

static bool is_regular_relative(const char *directory, const char *relative_path)
{
    char *path = join_path(directory, relative_path);
    struct stat information;
    bool regular = path != NULL && stat(path, &information) == 0 &&
        S_ISREG(information.st_mode);

    free(path);
    return regular;
}

static char *read_text_file(const char *path)
{
    struct stat information;
    FILE *file;
    char *contents;
    size_t length;
    size_t count;
    int read_error;
    int close_result;

    if (stat(path, &information) != 0 || information.st_size < 0 ||
        (uintmax_t)information.st_size > (uintmax_t)(SIZE_MAX - 1U)) {
        return NULL;
    }
    length = (size_t)information.st_size;
    contents = malloc(length + 1U);
    if (contents == NULL) {
        return NULL;
    }
    file = fopen(path, "r");
    if (file == NULL) {
        free(contents);
        return NULL;
    }
    count = fread(contents, 1U, length, file);
    read_error = ferror(file);
    close_result = fclose(file);
    if (count != length || read_error != 0 || close_result != 0) {
        free(contents);
        return NULL;
    }
    contents[length] = '\0';
    return contents;
}

static int write_text_file(const char *path, const char *contents)
{
    FILE *file = fopen(path, "w");
    int failed;

    if (file == NULL) return -1;
    failed = fputs(contents, file) == EOF;
    if (fclose(file) != 0) failed = 1;
    return failed ? -1 : 0;
}

static int select_verilator(const char *directory)
{
    static const char OLD_BACKEND[] = "backend = \"iverilog\"";
    static const char NEW_BACKEND[] = "backend = \"verilator\"";
    char *manifest_path = join_path(directory, "solar.toml");
    char *manifest = manifest_path == NULL
        ? NULL
        : read_text_file(manifest_path);
    char *match = manifest == NULL ? NULL : strstr(manifest, OLD_BACKEND);
    char *updated = NULL;
    int status = -1;

    if (match != NULL) {
        size_t prefix_length = (size_t)(match - manifest);
        size_t suffix_offset = prefix_length + strlen(OLD_BACKEND);
        size_t length = strlen(manifest) - strlen(OLD_BACKEND) +
            strlen(NEW_BACKEND);

        updated = malloc(length + 1U);
        if (updated != NULL) {
            (void)memcpy(updated, manifest, prefix_length);
            (void)memcpy(
                updated + prefix_length, NEW_BACKEND, strlen(NEW_BACKEND)
            );
            (void)strcpy(
                updated + prefix_length + strlen(NEW_BACKEND),
                manifest + suffix_offset
            );
            status = write_text_file(manifest_path, updated);
        }
    }
    free(updated);
    free(manifest);
    free(manifest_path);
    return status;
}

static int remove_waveform_dump(const char *directory)
{
    static const char TESTBENCH[] =
        "`timescale 1ns/1ps\n"
        "module counter_tb;\n"
        "    reg clk = 1'b0;\n"
        "    reg reset = 1'b1;\n"
        "    wire [3:0] value;\n"
        "    counter dut (.clk(clk), .reset(reset), .value(value));\n"
        "    always #5 clk = ~clk;\n"
        "    initial begin\n"
        "        #12 reset = 1'b0;\n"
        "        #100;\n"
        "        $display(\"counter value: %0d\", value);\n"
        "        $finish;\n"
        "    end\n"
        "endmodule\n";
    char *path = join_path(directory, "tb/basic.v");
    int status = path == NULL ? -1 : write_text_file(path, TESTBENCH);

    free(path);
    return status;
}

static void remove_initialized_project(const char *directory)
{
    const char *relative_files[] = {
        "tb/basic.v",
        "rtl/counter.v",
        "sim/.gitkeep",
        "synth/.gitkeep",
        ".gitignore",
        "README.md",
        "solar.toml"
    };
    const char *relative_directories[] = {"tb", "rtl", "sim", "synth"};
    bool removed = false;
    size_t artifact_count = 0U;
    size_t index;

    (void)solar_artifact_remove_registered(directory, &artifact_count);
    (void)solar_filesystem_clean_project(directory, &removed);
    for (index = 0U;
         index < sizeof(relative_files) / sizeof(relative_files[0]);
         index++) {
        char *path = join_path(directory, relative_files[index]);

        if (path != NULL) {
            (void)unlink(path);
        }
        free(path);
    }
    for (index = 0U;
         index < sizeof(relative_directories) / sizeof(relative_directories[0]);
         index++) {
        char *path = join_path(directory, relative_directories[index]);

        if (path != NULL) {
            (void)rmdir(path);
        }
        free(path);
    }
    (void)rmdir(directory);
}

static int verify_simulation_artifacts(const char *directory)
{
    const char *artifacts[] = {
        ".solar/tmp/tests/basic/simulation.vvp",
        "sim/basic.vcd",
        ".solar/logs/tests/basic/iverilog.stdout.log",
        ".solar/logs/tests/basic/iverilog.stderr.log",
        ".solar/logs/tests/basic/vvp.stdout.log",
        ".solar/logs/tests/basic/vvp.stderr.log"
    };
    size_t index;

    for (index = 0U; index < sizeof(artifacts) / sizeof(artifacts[0]); index++) {
        if (!is_regular_relative(directory, artifacts[index])) {
            (void)fprintf(stderr, "simulation: missing artifact %s\n", artifacts[index]);
            return 1;
        }
    }
    return 0;
}

static int verify_no_waveform_artifacts(const char *directory)
{
    const char *internal[] = {
        ".solar/tmp/tests/basic/simulation.vvp",
        ".solar/logs/tests/basic/iverilog.stdout.log",
        ".solar/logs/tests/basic/iverilog.stderr.log",
        ".solar/logs/tests/basic/vvp.stdout.log",
        ".solar/logs/tests/basic/vvp.stderr.log"
    };
    size_t index;

    for (index = 0U; index < sizeof(internal) / sizeof(internal[0]); index++) {
        if (!is_regular_relative(directory, internal[index])) {
            return report_failure(
                "simulation without waveform", "missing internal result or log"
            );
        }
    }
    if (is_regular_relative(directory, "sim/basic.vcd")) {
        return report_failure(
            "simulation without waveform", "published a waveform that was not generated"
        );
    }
    return 0;
}

static int verify_verilator_artifacts(const char *directory)
{
    const char *artifacts[] = {
        ".solar/tmp/tests/basic/simulation",
        "sim/basic.vcd",
        ".solar/logs/tests/basic/verilator.stdout.log",
        ".solar/logs/tests/basic/verilator.stderr.log",
        ".solar/logs/tests/basic/simulation.stdout.log",
        ".solar/logs/tests/basic/simulation.stderr.log"
    };
    size_t index;

    for (index = 0U; index < sizeof(artifacts) / sizeof(artifacts[0]); index++) {
        if (!is_regular_relative(directory, artifacts[index])) {
            (void)fprintf(stderr, "Verilator: missing artifact %s\n", artifacts[index]);
            return 1;
        }
    }
    return 0;
}

static int verify_synthesis_artifacts(const char *directory)
{
    const char *artifacts[] = {
        ".solar/tmp/synth/synthesis.ys",
        "synth/counter_netlist.v",
        "synth/statistics.txt",
        ".solar/logs/yosys/yosys.stdout.log",
        ".solar/logs/yosys/yosys.stderr.log"
    };
    char *script_path = NULL;
    char *script = NULL;
    size_t index;
    int failed = 0;

    for (index = 0U; index < sizeof(artifacts) / sizeof(artifacts[0]); index++) {
        if (!is_regular_relative(directory, artifacts[index])) {
            (void)fprintf(stderr, "synthesis: missing artifact %s\n", artifacts[index]);
            return 1;
        }
    }
    script_path = join_path(directory, ".solar/tmp/synth/synthesis.ys");
    if (script_path != NULL) {
        script = read_text_file(script_path);
    }
    if (script == NULL || strstr(script, "read_verilog") == NULL ||
        strstr(script, "rtl/counter.v") == NULL ||
        strstr(script, "counter_tb.v") != NULL || strstr(script, "/tb/") != NULL) {
        failed = report_failure(
            "synthesis", "Yosys script did not contain exactly the RTL source set"
        );
    }
    free(script);
    free(script_path);
    return failed;
}

int main(int argc, char *argv[])
{
    char directory_template[] = "/tmp/solar-integration-XXXXXX";
    char *solar_path;
    char *directory;
    const char *arguments[4];
    SolarProcessSpec specification = {0};
    SolarProcessResult process;
    SolarResult result;
    int return_code = 1;

    if (argc != 3 ||
        (strcmp(argv[2], "sim") != 0 &&
         strcmp(argv[2], "sim-no-waveform") != 0 &&
         strcmp(argv[2], "synth") != 0 &&
         strcmp(argv[2], "verilator") != 0)) {
        return report_failure(
            "integration",
            "expected the solar path and sim|sim-no-waveform|synth|verilator"
        );
    }
    solar_path = realpath(argv[1], NULL);
    if (solar_path == NULL) {
        return report_failure("integration", "could not resolve the solar executable");
    }
    directory = mkdtemp(directory_template);
    if (directory == NULL) {
        free(solar_path);
        return report_failure("integration", "mkdtemp failed");
    }

    result = solar_project_initialize(directory);
    if (result.status != SOLAR_STATUS_OK) {
        (void)fprintf(stderr, "integration init: %s\n", result.diagnostic.message);
        goto cleanup_directory;
    }
    if (strcmp(argv[2], "verilator") == 0 &&
        select_verilator(directory) != 0) {
        (void)fprintf(stderr, "integration: could not select Verilator\n");
        goto cleanup_directory;
    }
    if (strcmp(argv[2], "sim-no-waveform") == 0 &&
        remove_waveform_dump(directory) != 0) {
        (void)fprintf(stderr, "integration: could not remove waveform dumping\n");
        goto cleanup_directory;
    }

    arguments[0] = solar_path;
    arguments[1] = "build";
    arguments[2] = strcmp(argv[2], "synth") == 0 ? "synth" : "sim";
    arguments[3] = NULL;
    specification.executable = solar_path;
    specification.argv = arguments;
    specification.working_directory = directory;
    specification.stdout_log_path = NULL;
    specification.stderr_log_path = NULL;
    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);

    if (process.outcome == SOLAR_PROCESS_EXITED && process.exit_code == 4) {
        return_code = 77;
        goto cleanup_process;
    }
    if (result.status != SOLAR_STATUS_OK ||
        process.outcome != SOLAR_PROCESS_EXITED || process.exit_code != 0) {
        (void)fprintf(
            stderr,
            "%s integration: outcome %d, exit %d: %s\n",
            argv[2],
            (int)process.outcome,
            process.exit_code,
            result.diagnostic.message
        );
        if (process.stdout_text != NULL) {
            (void)fprintf(stderr, "stdout: %s", process.stdout_text);
        }
        if (process.stderr_text != NULL) {
            (void)fprintf(stderr, "stderr: %s", process.stderr_text);
        }
        goto cleanup_process;
    }

    if (strcmp(argv[2], "sim") == 0 ||
        strcmp(argv[2], "sim-no-waveform") == 0 ||
        strcmp(argv[2], "verilator") == 0) {
        if (process.stdout_text == NULL ||
            strstr(process.stdout_text, "counter value:") == NULL) {
            return_code = report_failure(
                "simulation", "$display output was not presented by the CLI"
            );
        } else {
            if (strcmp(argv[2], "sim-no-waveform") == 0) {
                if (process.stderr_text == NULL || strstr(
                        process.stderr_text,
                        "passed without generating a waveform"
                    ) == NULL) {
                    return_code = report_failure(
                        "simulation without waveform", "CLI warning was not emitted"
                    );
                } else {
                    return_code = verify_no_waveform_artifacts(directory);
                }
            } else {
                return_code = (strcmp(argv[2], "verilator") == 0
                    ? verify_verilator_artifacts(directory)
                    : verify_simulation_artifacts(directory)) == 0 ? 0 : 1;
            }
        }
    } else {
        return_code = verify_synthesis_artifacts(directory) == 0 ? 0 : 1;
    }

cleanup_process:
    solar_process_result_free(&process);
cleanup_directory:
    remove_initialized_project(directory);
    free(solar_path);
    return return_code;
}

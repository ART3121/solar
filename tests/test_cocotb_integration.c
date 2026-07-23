#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/filesystem.h"
#include "solar/runner.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char MANIFEST[] =
    "[solar]\nformat = 2\n"
    "[project]\nname = \"cocotb-adder\"\nlanguage = \"verilog\"\n"
    "default_test = \"python\"\n"
    "[sources]\nrtl = [\"rtl/adder.v\"]\n"
    "[simulation]\nbackend = \"verilator\"\n"
    "[synthesis]\nbackend = \"yosys\"\ntop = \"adder\"\n"
    "[[test]]\nname = \"python\"\ntop = \"adder\"\n"
    "driver = \"cocotb\"\ncocotb = \"tb/test_adder.py\"\n"
    "waveform = \"adder.fst\"\n";

static const char RTL[] =
    "module adder(input [7:0] a, input [7:0] b, output [8:0] sum);\n"
    "  assign sum = a + b;\nendmodule\n";

static const char PYTHON_TEST[] =
    "import cocotb\n"
    "from cocotb.triggers import Timer\n\n"
    "@cocotb.test()\n"
    "async def adds_values(dut):\n"
    "    dut.a.value = 9\n"
    "    dut.b.value = 4\n"
    "    await Timer(1, unit=\"ns\")\n"
    "    assert int(dut.sum.value) == 13\n";

static char *join_path(const char *left, const char *right)
{
    size_t length = strlen(left) + strlen(right) + 2U;
    char *path = malloc(length);

    if (path != NULL) (void)snprintf(path, length, "%s/%s", left, right);
    return path;
}

static int write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    int failed;

    if (file == NULL) return -1;
    failed = fputs(text, file) == EOF;
    if (fclose(file) != 0) failed = 1;
    return failed ? -1 : 0;
}

static bool command_available(const char *const arguments[])
{
    SolarProcessSpec specification = {
        arguments[0], arguments, NULL, NULL, NULL, NULL, 0U, NULL
    };
    SolarProcessResult process;
    SolarResult result;
    bool available;

    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    available = result.status == SOLAR_STATUS_OK;
    solar_process_result_free(&process);
    return available;
}

static int run_solar(const char *solar, const char *root)
{
    const char *arguments[] = {solar, "build", "sim", "python", NULL};
    const SolarEnvironmentAddition environment[] = {
        {"SOLAR_NO_VIEWER", "1"}
    };
    SolarProcessSpec specification = {
        solar, arguments, root, NULL, NULL, environment, 1U, NULL
    };
    SolarProcessResult process;
    SolarResult result;
    int code;

    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    code = result.status == SOLAR_STATUS_OK ? 0 : 1;
    if (code != 0) {
        (void)fprintf(
            stderr, "real cocotb integration failed\nstdout: %s\nstderr: %s\n",
            process.stdout_text == NULL ? "" : process.stdout_text,
            process.stderr_text == NULL ? "" : process.stderr_text
        );
    }
    solar_process_result_free(&process);
    return code;
}

int main(int argc, char **argv)
{
    const char *verilator_arguments[] = {"verilator", "--version", NULL};
    const char *cocotb_arguments[] = {
        "python3", "-c", "import cocotb_tools.runner", NULL
    };
    char root_template[] = "/tmp/solar-cocotb-real-XXXXXX";
    char *root = NULL;
    char *manifest = NULL;
    char *rtl = NULL;
    char *test = NULL;
    char *waveform = NULL;
    bool removed = false;
    int code = 1;

    if (argc != 2) return 1;
    if (!command_available(verilator_arguments) ||
        !command_available(cocotb_arguments)) {
        (void)printf(
            "SKIP: Verilator and cocotb 2.x are required for the real cocotb flow\n"
        );
        return 77;
    }
    root = mkdtemp(root_template);
    if (root == NULL) goto cleanup;
    manifest = join_path(root, "solar.toml");
    rtl = join_path(root, "rtl/adder.v");
    test = join_path(root, "tb/test_adder.py");
    waveform = join_path(root, "sim/adder.fst");
    {
        char *rtl_directory = join_path(root, "rtl");
        char *tb_directory = join_path(root, "tb");

        if (rtl_directory == NULL || tb_directory == NULL ||
            mkdir(rtl_directory, 0700) != 0 || mkdir(tb_directory, 0700) != 0 ||
            write_file(manifest, MANIFEST) != 0 || write_file(rtl, RTL) != 0 ||
            write_file(test, PYTHON_TEST) != 0) {
            free(tb_directory);
            free(rtl_directory);
            goto cleanup;
        }
        free(tb_directory);
        free(rtl_directory);
    }
    if (run_solar(argv[1], root) != 0 || access(waveform, R_OK) != 0) {
        goto cleanup;
    }
    code = 0;

cleanup:
    if (root != NULL) (void)solar_filesystem_clean_project(root, &removed);
    if (test != NULL) (void)unlink(test);
    if (rtl != NULL) (void)unlink(rtl);
    if (manifest != NULL) (void)unlink(manifest);
    if (root != NULL) {
        char *tb_directory = join_path(root, "tb");
        char *rtl_directory = join_path(root, "rtl");

        if (tb_directory != NULL) (void)rmdir(tb_directory);
        if (rtl_directory != NULL) (void)rmdir(rtl_directory);
        free(rtl_directory);
        free(tb_directory);
        (void)rmdir(root);
    }
    free(waveform);
    free(test);
    free(rtl);
    free(manifest);
    return code;
}

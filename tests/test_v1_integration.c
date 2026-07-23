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
    "[project]\n"
    "name = \"legacy-counter\"\n"
    "top = \"counter\"\n"
    "language = \"verilog\"\n"
    "\n"
    "[sources]\n"
    "rtl = [\"rtl/counter.v\"]\n"
    "testbench = [\"tb/counter_tb.v\"]\n"
    "\n"
    "[simulation]\n"
    "backend = \"iverilog\"\n"
    "top = \"counter_tb\"\n"
    "waveform = \".solar/build/sim/counter.vcd\"\n"
    "\n"
    "[synthesis]\n"
    "backend = \"yosys\"\n"
    "top = \"counter\"\n";

static const char RTL[] =
    "module counter(input wire clk, output reg value);\n"
    "always @(posedge clk) value <= ~value;\n"
    "endmodule\n";

static const char TESTBENCH[] =
    "module counter_tb;\n"
    "reg clk = 0;\n"
    "wire value;\n"
    "counter dut(.clk(clk), .value(value));\n"
    "always #1 clk = ~clk;\n"
    "initial begin\n"
    "  $dumpfile(\".solar/build/sim/counter.vcd\");\n"
    "  $dumpvars(0, counter_tb);\n"
    "  #4 $finish;\n"
    "end\n"
    "endmodule\n";

static char *join_path(const char *left, const char *right)
{
    size_t length = strlen(left) + strlen(right) + 2U;
    char *path = malloc(length);

    if (path != NULL) (void)snprintf(path, length, "%s/%s", left, right);
    return path;
}

static int write_file(const char *root, const char *relative, const char *text)
{
    char *path = join_path(root, relative);
    FILE *file;
    int failed;

    if (path == NULL) return -1;
    file = fopen(path, "w");
    free(path);
    if (file == NULL) return -1;
    failed = fputs(text, file) == EOF;
    if (fclose(file) != 0) failed = 1;
    return failed ? -1 : 0;
}

static bool regular_file(const char *root, const char *relative)
{
    char *path = join_path(root, relative);
    struct stat information;
    bool regular = path != NULL && stat(path, &information) == 0 &&
        S_ISREG(information.st_mode);

    free(path);
    return regular;
}

static void cleanup_project(const char *root)
{
    const char *files[] = {
        "solar.toml", "rtl/counter.v", "tb/counter_tb.v"
    };
    const char *directories[] = {"rtl", "tb"};
    bool removed = false;
    size_t index;

    (void)solar_filesystem_clean_project(root, &removed);
    for (index = 0U; index < sizeof(files) / sizeof(files[0]); index++) {
        char *path = join_path(root, files[index]);

        if (path != NULL) (void)unlink(path);
        free(path);
    }
    for (index = 0U;
         index < sizeof(directories) / sizeof(directories[0]);
         index++) {
        char *path = join_path(root, directories[index]);

        if (path != NULL) (void)rmdir(path);
        free(path);
    }
    (void)rmdir(root);
}

int main(int argc, char **argv)
{
    char template[] = "/tmp/solar-v1-integration-XXXXXX";
    char *root;
    char *solar;
    const char *arguments[4];
    SolarProcessSpec specification = {0};
    SolarProcessResult process;
    SolarResult result;
    int return_code = 1;

    if (argc != 2) return 1;
    solar = realpath(argv[1], NULL);
    root = mkdtemp(template);
    if (solar == NULL || root == NULL) {
        free(solar);
        return 1;
    }
    {
        char *rtl = join_path(root, "rtl");
        char *tb = join_path(root, "tb");
        int failed = rtl == NULL || tb == NULL ||
            mkdir(rtl, 0700) != 0 || mkdir(tb, 0700) != 0;

        free(tb);
        free(rtl);
        if (failed ||
            write_file(root, "solar.toml", MANIFEST) != 0 ||
            write_file(root, "rtl/counter.v", RTL) != 0 ||
            write_file(root, "tb/counter_tb.v", TESTBENCH) != 0) {
            goto cleanup;
        }
    }
    arguments[0] = solar;
    arguments[1] = "build";
    arguments[2] = "sim";
    arguments[3] = NULL;
    specification.executable = solar;
    specification.argv = arguments;
    specification.working_directory = root;
    specification.stdout_log_path = NULL;
    specification.stderr_log_path = NULL;
    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    if (process.outcome == SOLAR_PROCESS_EXITED && process.exit_code == 4) {
        return_code = 77;
    } else if (result.status == SOLAR_STATUS_OK &&
        process.outcome == SOLAR_PROCESS_EXITED && process.exit_code == 0 &&
        process.stderr_text != NULL &&
        strstr(process.stderr_text, "uses project format 1") != NULL &&
        regular_file(root, ".solar/tmp/tests/default/simulation.vvp") &&
        regular_file(root, "sim/counter.vcd")) {
        return_code = 0;
    } else {
        (void)fprintf(
            stderr,
            "v1 integration failed: %s\nstdout: %s\nstderr: %s\n",
            result.diagnostic.message,
            process.stdout_text == NULL ? "" : process.stdout_text,
            process.stderr_text == NULL ? "" : process.stderr_text
        );
    }
    solar_process_result_free(&process);

cleanup:
    cleanup_project(root);
    free(solar);
    return return_code;
}

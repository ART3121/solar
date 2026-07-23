#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/filesystem.h"
#include "solar/runner.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char MANIFEST[] =
    "[solar]\n"
    "format = 2\n"
    "\n"
    "[project]\n"
    "name = \"counter\"\n"
    "language = \"verilog\"\n"
    "default_test = \"basic\"\n"
    "default_profile = \"debug\"\n"
    "\n"
    "[sources]\n"
    "rtl = [\"rtl/counter.v\"]\n"
    "include_dirs = [\"rtl/include files\"]\n"
    "defines = [\"DATA_WIDTH=8\"]\n"
    "\n"
    "[simulation]\n"
    "backend = \"iverilog\"\n"
    "\n"
    "[synthesis]\n"
    "backend = \"yosys\"\n"
    "top = \"counter\"\n"
    "\n"
    "[[profile]]\n"
    "name = \"debug\"\n"
    "defines = [\"DEBUG\"]\n"
    "include_dirs = [\"profiles/debug include\"]\n"
    "\n"
    "[[profile]]\n"
    "name = \"release\"\n"
    "defines = [\"SYNTHESIS\"]\n"
    "include_dirs = [\"profiles/release include\"]\n"
    "\n"
    "[[test]]\n"
    "name = \"basic\"\n"
    "top = \"counter_tb\"\n"
    "sources = [\"tb/counter_tb.v\"]\n"
    "include_dirs = [\"tb/test include\"]\n"
    "defines = [\"TEST_BASIC\"]\n"
    "waveform = \"basic.vcd\"\n"
    "\n"
    "[[test]]\n"
    "name = \"overflow\"\n"
    "top = \"counter_overflow_tb\"\n"
    "sources = [\"tb/counter_overflow_tb.v\"]\n"
    "include_dirs = [\"tb/test include\"]\n"
    "defines = [\"TEST_OVERFLOW\"]\n"
    "waveform = \"overflow.vcd\"\n";

static const char RTL[] =
    "`include \"counter_width.vh\"\n"
    "module counter(\n"
    "    input wire clk,\n"
    "    input wire reset,\n"
    "    output reg [`COUNTER_WIDTH-1:0] value\n"
    ");\n"
    "`ifdef SYNTHESIS\n"
    "`include \"release_marker.vh\"\n"
    "`ifndef RELEASE_INCLUDE\n"
    "missing_release_include missing_release_include_instance();\n"
    "`endif\n"
    "`endif\n"
    "always @(posedge clk) begin\n"
    "    if (reset) value <= {`COUNTER_WIDTH{1'b0}};\n"
    "    else value <= value + 1'b1;\n"
    "end\n"
    "endmodule\n";

static const char WIDTH_HEADER[] =
    "`define COUNTER_WIDTH `DATA_WIDTH\n";

static const char DEBUG_HEADER[] =
    "`define DEBUG_INCLUDE\n";

static const char RELEASE_HEADER[] =
    "`define RELEASE_INCLUDE\n";

static const char TEST_HEADER[] =
    "`define TEST_INCLUDE\n";

static const char BASIC_TESTBENCH[] =
    "`include \"debug_marker.vh\"\n"
    "`include \"test_marker.vh\"\n"
    "module counter_tb;\n"
    "reg clk = 0;\n"
    "reg reset = 1;\n"
    "wire [`DATA_WIDTH-1:0] value;\n"
    "counter dut(.clk(clk), .reset(reset), .value(value));\n"
    "`ifndef DEBUG\n"
    "missing_debug_define missing_debug_define_instance();\n"
    "`endif\n"
    "`ifndef TEST_BASIC\n"
    "missing_basic_define missing_basic_define_instance();\n"
    "`endif\n"
    "`ifndef DEBUG_INCLUDE\n"
    "missing_debug_include missing_debug_include_instance();\n"
    "`endif\n"
    "`ifndef TEST_INCLUDE\n"
    "missing_test_include missing_test_include_instance();\n"
    "`endif\n"
    "always #1 clk = ~clk;\n"
    "initial begin\n"
    "    $dumpfile(\"basic.vcd\");\n"
    "    $dumpvars(0, counter_tb);\n"
    "    #2 reset = 0;\n"
    "    #4;\n"
    "    if (value !== 8'd2) $fatal(1, \"unexpected counter value\");\n"
    "    $finish;\n"
    "end\n"
    "endmodule\n";

static const char OVERFLOW_TESTBENCH[] =
    "`include \"debug_marker.vh\"\n"
    "`include \"test_marker.vh\"\n"
    "module counter_overflow_tb;\n"
    "reg clk = 0;\n"
    "reg reset = 1;\n"
    "wire [`DATA_WIDTH-1:0] value;\n"
    "counter dut(.clk(clk), .reset(reset), .value(value));\n"
    "`ifndef DEBUG\n"
    "missing_debug_define missing_debug_define_instance();\n"
    "`endif\n"
    "`ifndef TEST_OVERFLOW\n"
    "missing_overflow_define missing_overflow_define_instance();\n"
    "`endif\n"
    "`ifndef DEBUG_INCLUDE\n"
    "missing_debug_include missing_debug_include_instance();\n"
    "`endif\n"
    "`ifndef TEST_INCLUDE\n"
    "missing_test_include missing_test_include_instance();\n"
    "`endif\n"
    "always #1 clk = ~clk;\n"
    "initial begin\n"
    "    $dumpfile(\"overflow.vcd\");\n"
    "    $dumpvars(0, counter_overflow_tb);\n"
    "    #2 reset = 0;\n"
    "    #4;\n"
    "    if (value !== 8'd2) $fatal(1, \"unexpected counter value\");\n"
    "    $finish;\n"
    "end\n"
    "endmodule\n";

static char *join_path(const char *left, const char *right)
{
    size_t length = strlen(left) + strlen(right) + 2U;
    char *path = malloc(length);

    if (path != NULL) (void)snprintf(path, length, "%s/%s", left, right);
    return path;
}

static int report_failure(const char *message)
{
    (void)fprintf(stderr, "v2 integration: %s\n", message);
    return 1;
}

static int make_directory(const char *root, const char *relative)
{
    char *path = join_path(root, relative);
    int result = path == NULL ? -1 : mkdir(path, 0700);

    free(path);
    return result;
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

static char *read_file(const char *root, const char *relative)
{
    char *path = join_path(root, relative);
    struct stat information;
    char *contents = NULL;
    FILE *file = NULL;
    size_t length;

    if (path == NULL || stat(path, &information) != 0 ||
        information.st_size < 0 ||
        (uintmax_t)information.st_size > (uintmax_t)(SIZE_MAX - 1U)) {
        free(path);
        return NULL;
    }
    length = (size_t)information.st_size;
    contents = malloc(length + 1U);
    if (contents != NULL) file = fopen(path, "r");
    free(path);
    if (contents == NULL || file == NULL ||
        fread(contents, 1U, length, file) != length || ferror(file) != 0) {
        free(contents);
        if (file != NULL) (void)fclose(file);
        return NULL;
    }
    contents[length] = '\0';
    if (fclose(file) != 0) {
        free(contents);
        return NULL;
    }
    return contents;
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

static bool missing_path(const char *root, const char *relative)
{
    char *path = join_path(root, relative);
    struct stat information;
    bool missing = path != NULL && lstat(path, &information) != 0;

    free(path);
    return missing;
}

static int create_fixture(const char *root)
{
    static const char *const DIRECTORIES[] = {
        "rtl", "rtl/include files", "tb", "tb/test include", "profiles",
        "profiles/debug include", "profiles/release include"
    };
    size_t index;

    for (index = 0U;
         index < sizeof(DIRECTORIES) / sizeof(DIRECTORIES[0]);
         index++) {
        if (make_directory(root, DIRECTORIES[index]) != 0) return -1;
    }
    if (write_file(root, "solar.toml", MANIFEST) != 0 ||
        write_file(root, "rtl/counter.v", RTL) != 0 ||
        write_file(root, "rtl/include files/counter_width.vh", WIDTH_HEADER) != 0 ||
        write_file(root, "profiles/debug include/debug_marker.vh", DEBUG_HEADER) != 0 ||
        write_file(root, "profiles/release include/release_marker.vh", RELEASE_HEADER) != 0 ||
        write_file(root, "tb/test include/test_marker.vh", TEST_HEADER) != 0 ||
        write_file(root, "tb/counter_tb.v", BASIC_TESTBENCH) != 0 ||
        write_file(root, "tb/counter_overflow_tb.v", OVERFLOW_TESTBENCH) != 0) {
        return -1;
    }
    return 0;
}

static void cleanup_fixture(const char *root)
{
    static const char *const FILES[] = {
        "rtl/include files/counter_width.vh",
        "profiles/debug include/debug_marker.vh",
        "profiles/release include/release_marker.vh",
        "tb/test include/test_marker.vh",
        "tb/counter_tb.v",
        "tb/counter_overflow_tb.v",
        "rtl/counter.v",
        "solar.toml"
    };
    static const char *const DIRECTORIES[] = {
        "rtl/include files", "profiles/debug include",
        "profiles/release include", "tb/test include", "rtl", "tb",
        "profiles"
    };
    bool removed = false;
    size_t index;

    (void)solar_filesystem_clean_project(root, &removed);
    for (index = 0U; index < sizeof(FILES) / sizeof(FILES[0]); index++) {
        char *path = join_path(root, FILES[index]);

        if (path != NULL) (void)unlink(path);
        free(path);
    }
    for (index = 0U;
         index < sizeof(DIRECTORIES) / sizeof(DIRECTORIES[0]);
         index++) {
        char *path = join_path(root, DIRECTORIES[index]);

        if (path != NULL) (void)rmdir(path);
        free(path);
    }
    (void)rmdir(root);
}

static int run_command(
    const char *solar,
    const char *root,
    const char *const arguments[],
    const char *stdout_fragment
)
{
    SolarProcessSpec specification = {0};
    SolarProcessResult process;
    SolarResult result;
    int failed = 0;

    specification.executable = solar;
    specification.argv = arguments;
    specification.working_directory = root;
    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    if (result.status != SOLAR_STATUS_OK ||
        process.outcome != SOLAR_PROCESS_EXITED || process.exit_code != 0 ||
        (stdout_fragment != NULL &&
            (process.stdout_text == NULL ||
             strstr(process.stdout_text, stdout_fragment) == NULL))) {
        (void)fprintf(
            stderr,
            "v2 integration command failed (exit %d): %s\nstdout: %s\nstderr: %s\n",
            process.exit_code,
            result.diagnostic.message,
            process.stdout_text == NULL ? "" : process.stdout_text,
            process.stderr_text == NULL ? "" : process.stderr_text
        );
        failed = 1;
    }
    solar_process_result_free(&process);
    return failed;
}

static int run_eda_command(
    const char *solar,
    const char *root,
    const char *const arguments[],
    const char *stdout_fragment,
    bool *tool_missing_out
)
{
    SolarProcessSpec specification = {0};
    SolarProcessResult process;
    SolarResult result;
    int failed = 0;

    *tool_missing_out = false;
    specification.executable = solar;
    specification.argv = arguments;
    specification.working_directory = root;
    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    if (process.outcome == SOLAR_PROCESS_EXITED && process.exit_code == 4) {
        *tool_missing_out = true;
    } else if (result.status != SOLAR_STATUS_OK ||
        process.outcome != SOLAR_PROCESS_EXITED || process.exit_code != 0 ||
        process.stdout_text == NULL ||
        strstr(process.stdout_text, stdout_fragment) == NULL) {
        (void)fprintf(
            stderr,
            "v2 EDA command failed (exit %d): %s\nstdout: %s\nstderr: %s\n",
            process.exit_code,
            result.diagnostic.message,
            process.stdout_text == NULL ? "" : process.stdout_text,
            process.stderr_text == NULL ? "" : process.stderr_text
        );
        failed = 1;
    }
    solar_process_result_free(&process);
    return failed;
}

static int verify_test_artifacts(const char *root)
{
    static const char *const REQUIRED[] = {
        ".solar/tmp/tests/basic/simulation.vvp",
        "sim/basic.vcd",
        ".solar/logs/tests/basic/iverilog.stdout.log",
        ".solar/logs/tests/basic/iverilog.stderr.log",
        ".solar/logs/tests/basic/vvp.stdout.log",
        ".solar/logs/tests/basic/vvp.stderr.log",
        ".solar/tmp/tests/overflow/simulation.vvp",
        "sim/overflow.vcd",
        ".solar/logs/tests/overflow/iverilog.stdout.log",
        ".solar/logs/tests/overflow/iverilog.stderr.log",
        ".solar/logs/tests/overflow/vvp.stdout.log",
        ".solar/logs/tests/overflow/vvp.stderr.log"
    };
    size_t index;

    for (index = 0U; index < sizeof(REQUIRED) / sizeof(REQUIRED[0]); index++) {
        if (!regular_file(root, REQUIRED[index])) {
            (void)fprintf(stderr, "v2 integration: missing %s\n", REQUIRED[index]);
            return 1;
        }
    }
    if (!missing_path(root, "sim/basic/overflow.vcd") ||
        !missing_path(root, "sim/overflow/basic.vcd")) {
        return report_failure("test waveforms were not isolated by test name");
    }
    return 0;
}

static int verify_synthesis(
    const char *root,
    const char *first_script,
    const char *second_script
)
{
    if (!regular_file(root, "synth/counter_netlist.v") ||
        !regular_file(root, "synth/statistics.txt") ||
        !regular_file(root, ".solar/logs/yosys/yosys.stdout.log") ||
        !regular_file(root, ".solar/logs/yosys/yosys.stderr.log")) {
        return report_failure("synthesis artifacts are incomplete");
    }
    if (first_script == NULL || second_script == NULL ||
        strcmp(first_script, second_script) != 0) {
        return report_failure("Yosys script generation is not deterministic");
    }
    if (strstr(first_script, "verilog_defaults -add") == NULL ||
        strstr(first_script, "-Iincludes/0") == NULL ||
        strstr(first_script, "-Iincludes/1") == NULL ||
        strstr(first_script, "-DDATA_WIDTH=8") == NULL ||
        strstr(first_script, "-DSYNTHESIS") == NULL ||
        strstr(first_script, "-DDEBUG") != NULL ||
        strstr(first_script, "-DTEST_BASIC") != NULL ||
        strstr(first_script, "counter_tb.v") != NULL ||
        strstr(first_script, "counter_overflow_tb.v") != NULL) {
        return report_failure(
            "Yosys script has incorrect includes, defines, or source selection"
        );
    }
    return 0;
}

int main(int argc, char **argv)
{
    char directory_template[] = "/tmp/solar v2 integration-XXXXXX";
    char *root = NULL;
    char *solar = NULL;
    char *first_script = NULL;
    char *second_script = NULL;
    bool tool_missing = false;
    int return_code = 1;
    const char *check[] = {NULL, "check", NULL};
    const char *list[] = {NULL, "build", "sim", "--list", NULL};
    const char *test_default[] = {NULL, "build", "sim", NULL};
    const char *test_basic[] = {NULL, "build", "sim", "basic", NULL};
    const char *test_overflow[] = {NULL, "build", "sim", "overflow", NULL};
    const char *test_all[] = {NULL, "build", "sim", "--all", NULL};
    const char *test_profile[] = {
        NULL, "build", "sim", "basic", "--profile", "debug", NULL
    };
    const char *simulate[] = {NULL, "build", "sim", NULL};
    const char *synthesize[] = {
        NULL, "build", "synth", "--profile", "release", NULL
    };

    if (argc != 2) return report_failure("expected the Solar executable path");
    solar = realpath(argv[1], NULL);
    root = mkdtemp(directory_template);
    if (solar == NULL || root == NULL || create_fixture(root) != 0) {
        report_failure("could not create the temporary fixture");
        goto cleanup;
    }
    check[0] = solar;
    list[0] = solar;
    test_default[0] = solar;
    test_basic[0] = solar;
    test_overflow[0] = solar;
    test_all[0] = solar;
    test_profile[0] = solar;
    simulate[0] = solar;
    synthesize[0] = solar;

    if (run_command(solar, root, check, "2 tests") != 0 ||
        run_command(solar, root, list,
            "basic        top: counter_tb [default]") != 0 ||
        run_command(solar, root, list,
            "overflow     top: counter_overflow_tb") != 0) {
        goto cleanup;
    }
    if (run_eda_command(
            solar, root, test_default, "PASS  basic", &tool_missing
        ) != 0) {
        goto cleanup;
    }
    if (tool_missing) {
        return_code = 77;
        goto cleanup;
    }
    if (run_eda_command(
            solar, root, test_basic, "PASS  basic", &tool_missing
        ) != 0 || tool_missing) goto eda_failure;
    if (run_eda_command(
            solar, root, test_overflow, "PASS  overflow", &tool_missing
        ) != 0 || tool_missing) goto eda_failure;
    if (run_eda_command(
            solar, root, test_all, "2 passed, 0 failed", &tool_missing
        ) != 0 || tool_missing) goto eda_failure;
    if (run_eda_command(
            solar, root, test_profile, "PASS  basic", &tool_missing
        ) != 0 || tool_missing) goto eda_failure;
    if (run_eda_command(
            solar, root, simulate, "PASS  basic",
            &tool_missing
        ) != 0 || tool_missing) goto eda_failure;
    if (verify_test_artifacts(root) != 0) goto cleanup;

    if (run_eda_command(
            solar, root, synthesize, "Netlist:", &tool_missing
        ) != 0) {
        goto cleanup;
    }
    if (tool_missing) {
        return_code = 77;
        goto cleanup;
    }
    first_script = read_file(root, ".solar/tmp/synth/synthesis.ys");
    if (run_eda_command(
            solar, root, synthesize, "Netlist:", &tool_missing
        ) != 0 || tool_missing) {
        if (tool_missing) return_code = 77;
        goto cleanup;
    }
    second_script = read_file(root, ".solar/tmp/synth/synthesis.ys");
    if (verify_synthesis(root, first_script, second_script) != 0) goto cleanup;
    return_code = 0;
    goto cleanup;

eda_failure:
    if (tool_missing) return_code = 77;

cleanup:
    free(second_script);
    free(first_script);
    if (root != NULL) cleanup_fixture(root);
    free(solar);
    return return_code;
}

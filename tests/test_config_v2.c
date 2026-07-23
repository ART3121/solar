#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/config.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char VERILOG_V2_MANIFEST[] =
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
    "rtl = [\n"
    "    \"rtl/counter.v\",\n"
    "    \"rtl/counter_control.v\"\n"
    "]\n"
    "include_dirs = [\n"
    "    \"rtl/include\"\n"
    "]\n"
    "defines = [\n"
    "    \"DATA_WIDTH=8\"\n"
    "]\n"
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
    "include_dirs = [\"profiles/debug/include\"]\n"
    "\n"
    "[[profile]]\n"
    "name = \"release\"\n"
    "defines = [\"SYNTHESIS\"]\n"
    "\n"
    "[[test]]\n"
    "name = \"basic\"\n"
    "top = \"counter_tb\"\n"
    "sources = [\n"
    "    \"tb/counter_tb.v\",\n"
    "    \"tb/checker.v\"\n"
    "]\n"
    "include_dirs = [\"tb/include\"]\n"
    "defines = [\"TEST_BASIC\"]\n"
    "waveform = \"waves/basic.vcd\"\n"
    "\n"
    "[[test]]\n"
    "name = \"overflow\"\n"
    "top = \"counter_overflow_tb\"\n"
    "sources = [\"tb/counter_overflow_tb.v\"]\n"
    "defines = [\"TEST_OVERFLOW\"]\n"
    "waveform = \"overflow.vcd\"\n";

static const char YANC_V2_MANIFEST[] =
    "[solar]\n"
    "format = 2\n"
    "\n"
    "[project]\n"
    "name = \"sapho-processor\"\n"
    "language = \"cmm\"\n"
    "default_test = \"generated\"\n"
    "default_profile = \"debug\"\n"
    "\n"
    "[compiler]\n"
    "backend = \"yanc\"\n"
    "source = \"src/processor.cmm\"\n"
    "processor = \"processor\"\n"
    "frequency_mhz = 100\n"
    "simulation_clocks = 100000\n"
    "include_dirs = [\"src/include\", \"src/include with spaces\"]\n"
    "\n"
    "[yanc]\n"
    "root = \"/opt/YANC Toolchain\"\n"
    "diagnostics = \"en\"\n"
    "\n"
    "[simulation]\n"
    "backend = \"iverilog\"\n"
    "\n"
    "[synthesis]\n"
    "backend = \"yosys\"\n"
    "top = \"processor\"\n"
    "\n"
    "[[profile]]\n"
    "name = \"debug\"\n"
    "defines = [\"YANC_DEBUG\"]\n";

static const char COCOTB_V2_MANIFEST[] =
    "[solar]\n"
    "format = 2\n"
    "[project]\n"
    "name = \"python-test\"\n"
    "language = \"verilog\"\n"
    "default_test = \"cocotb-basic\"\n"
    "[sources]\n"
    "rtl = [\"rtl/dut.v\"]\n"
    "[simulation]\n"
    "backend = \"verilator\"\n"
    "[synthesis]\n"
    "backend = \"yosys\"\n"
    "top = \"dut\"\n"
    "[[test]]\n"
    "name = \"cocotb-basic\"\n"
    "top = \"dut\"\n"
    "driver = \"cocotb\"\n"
    "cocotb = \"tb/test_dut.py\"\n"
    "waveform = \"dut.fst\"\n";

static const char COCOTB_WRONG_BACKEND_MANIFEST[] =
    "[solar]\n"
    "format = 2\n"
    "[project]\n"
    "name = \"python-test\"\n"
    "language = \"verilog\"\n"
    "[sources]\n"
    "rtl = [\"rtl/dut.v\"]\n"
    "[simulation]\n"
    "backend = \"iverilog\"\n"
    "[synthesis]\n"
    "backend = \"yosys\"\n"
    "top = \"dut\"\n"
    "[[test]]\n"
    "name = \"python\"\n"
    "top = \"dut\"\n"
    "driver = \"cocotb\"\n"
    "cocotb = \"tb/test_dut.py\"\n"
    "waveform = \"dut.vcd\"\n";

static const char COCOTB_INVALID_MODULE_MANIFEST[] =
    "[solar]\n"
    "format = 2\n"
    "[project]\n"
    "name = \"python-test\"\n"
    "language = \"verilog\"\n"
    "[sources]\n"
    "rtl = [\"rtl/dut.v\"]\n"
    "[simulation]\n"
    "backend = \"verilator\"\n"
    "[synthesis]\n"
    "backend = \"yosys\"\n"
    "top = \"dut\"\n"
    "[[test]]\n"
    "name = \"python\"\n"
    "top = \"dut\"\n"
    "driver = \"cocotb\"\n"
    "cocotb = \"tb/test-dut.py\"\n"
    "waveform = \"dut.fst\"\n";

static const char V1_MANIFEST[] =
    "[project]\n"
    "name = \"legacy-counter\"\n"
    "top = \"counter\"\n"
    "language = \"verilog\"\n"
    "\n"
    "[sources]\n"
    "rtl = [\"rtl/counter.v\"]\n"
    "testbench = [\"tb/counter_tb.v\", \"tb/checker.v\"]\n"
    "\n"
    "[simulation]\n"
    "backend = \"iverilog\"\n"
    "top = \"counter_tb\"\n"
    "waveform = \".solar/build/sim/counter.vcd\"\n"
    "\n"
    "[synthesis]\n"
    "backend = \"yosys\"\n"
    "top = \"counter\"\n";

static const char UNSUPPORTED_FORMAT_MANIFEST[] =
    "[solar]\n"
    "format = 3\n"
    "[project]\n"
    "name = \"counter\"\n"
    "language = \"verilog\"\n"
    "default_test = \"basic\"\n"
    "[sources]\n"
    "rtl = [\"rtl/counter.v\"]\n"
    "[simulation]\n"
    "backend = \"iverilog\"\n"
    "[synthesis]\n"
    "backend = \"yosys\"\n"
    "top = \"counter\"\n"
    "[[test]]\n"
    "name = \"basic\"\n"
    "top = \"counter_tb\"\n"
    "sources = [\"tb/counter_tb.v\"]\n";

static const char ZERO_FORMAT_MANIFEST[] =
    "[solar]\n"
    "format = 0\n"
    "[project]\n"
    "name = \"counter\"\n"
    "top = \"counter\"\n"
    "language = \"verilog\"\n"
    "[sources]\n"
    "rtl = [\"rtl/counter.v\"]\n"
    "testbench = [\"tb/counter_tb.v\"]\n"
    "[simulation]\n"
    "backend = \"iverilog\"\n"
    "top = \"counter_tb\"\n"
    "waveform = \".solar/build/sim/counter.vcd\"\n"
    "[synthesis]\n"
    "backend = \"yosys\"\n"
    "top = \"counter\"\n";

static const char DUPLICATE_TEST_MANIFEST[] =
    "[solar]\n"
    "format = 2\n"
    "[project]\n"
    "name = \"counter\"\n"
    "language = \"verilog\"\n"
    "[sources]\n"
    "rtl = [\"rtl/counter.v\"]\n"
    "[simulation]\n"
    "backend = \"iverilog\"\n"
    "[synthesis]\n"
    "backend = \"yosys\"\n"
    "top = \"counter\"\n"
    "[[test]]\n"
    "name = \"same\"\n"
    "top = \"first_tb\"\n"
    "sources = [\"tb/first_tb.v\"]\n"
    "[[test]]\n"
    "name = \"same\"\n"
    "top = \"second_tb\"\n"
    "sources = [\"tb/second_tb.v\"]\n";

static const char DUPLICATE_PROFILE_MANIFEST[] =
    "[solar]\n"
    "format = 2\n"
    "[project]\n"
    "name = \"counter\"\n"
    "language = \"verilog\"\n"
    "[sources]\n"
    "rtl = [\"rtl/counter.v\"]\n"
    "[simulation]\n"
    "backend = \"iverilog\"\n"
    "[synthesis]\n"
    "backend = \"yosys\"\n"
    "top = \"counter\"\n"
    "[[profile]]\n"
    "name = \"debug\"\n"
    "[[profile]]\n"
    "name = \"debug\"\n"
    "[[test]]\n"
    "name = \"basic\"\n"
    "top = \"counter_tb\"\n"
    "sources = [\"tb/counter_tb.v\"]\n";

static const char DUPLICATE_TABLE_MANIFEST[] =
    "[solar]\n"
    "format = 2\n"
    "[project]\n"
    "name = \"counter\"\n"
    "language = \"verilog\"\n"
    "[project]\n"
    "default_test = \"basic\"\n";

static int report_failure(const char *test_name, const char *message)
{
    (void)fprintf(stderr, "%s: %s\n", test_name, message);
    return 1;
}

static int write_text_file(const char *path, const char *content)
{
    FILE *file = fopen(path, "w");

    if (file == NULL) {
        return -1;
    }
    if (fputs(content, file) == EOF) {
        (void)fclose(file);
        return -1;
    }
    return fclose(file);
}

static int evaluate_manifest(
    const char *test_name,
    const char *manifest_text,
    bool validate,
    SolarConfig *config,
    SolarResult *result_out
)
{
    char directory_template[] = "/tmp/solar-config-v2-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *manifest = NULL;
    size_t path_size;
    int failed = 0;

    solar_config_init(config);
    if (directory == NULL) {
        return report_failure(test_name, "mkdtemp failed");
    }
    path_size = strlen(directory) + sizeof("/solar.toml");
    manifest = malloc(path_size);
    if (manifest == NULL) {
        failed = report_failure(test_name, "could not allocate manifest path");
        goto cleanup;
    }
    (void)snprintf(manifest, path_size, "%s/solar.toml", directory);
    if (write_text_file(manifest, manifest_text) != 0) {
        failed = report_failure(test_name, "could not write temporary manifest");
        goto cleanup;
    }

    *result_out = solar_config_parse_file(manifest, config);
    if (validate && result_out->status == SOLAR_STATUS_OK) {
        *result_out = solar_config_validate(config, manifest);
    }

cleanup:
    if (manifest != NULL && unlink(manifest) != 0 && errno != ENOENT) {
        failed = report_failure(test_name, "could not remove temporary manifest");
    }
    if (rmdir(directory) != 0 && errno != ENOENT) {
        failed = report_failure(test_name, "could not remove temporary directory");
    }
    free(manifest);
    return failed;
}

static bool string_list_equals(
    const SolarStringList *list,
    const char *const *expected,
    size_t expected_count
)
{
    size_t index;

    if (list->count != expected_count) {
        return false;
    }
    for (index = 0U; index < expected_count; index++) {
        if (strcmp(list->items[index], expected[index]) != 0) {
            return false;
        }
    }
    return true;
}

static int test_verilog_v2_multiline_and_named_entries(void)
{
    const char *test_name = "Verilog format 2 multiline manifest";
    const char *const expected_rtl[] = {
        "rtl/counter.v", "rtl/counter_control.v"
    };
    const char *const expected_basic_sources[] = {
        "tb/counter_tb.v", "tb/checker.v"
    };
    SolarConfig config;
    SolarResult result;
    const SolarProfile *debug;
    const SolarTest *basic;
    int failed;

    failed = evaluate_manifest(
        test_name, VERILOG_V2_MANIFEST, false, &config, &result
    );
    if (failed != 0) {
        solar_config_free(&config);
        return failed;
    }
    if (result.status != SOLAR_STATUS_OK) {
        failed = report_failure(test_name, result.diagnostic.message);
        goto cleanup;
    }
    if (config.manifest_format != 2U ||
        strcmp(config.project.name, "counter") != 0 ||
        strcmp(config.project.language, "verilog") != 0 ||
        strcmp(config.project.default_test, "basic") != 0 ||
        strcmp(config.project.default_profile, "debug") != 0) {
        failed = report_failure(test_name, "project format-2 fields were lost");
        goto cleanup;
    }
    if (!string_list_equals(
            &config.sources.rtl,
            expected_rtl,
            sizeof(expected_rtl) / sizeof(expected_rtl[0])
        ) ||
        config.profiles == NULL || config.profile_count != 2U ||
        config.tests == NULL || config.test_count != 2U) {
        failed = report_failure(test_name, "repeated tables or multiline RTL were lost");
        goto cleanup;
    }

    debug = solar_config_find_profile(&config, "debug");
    basic = solar_config_find_test(&config, "basic");
    if (debug == NULL || basic == NULL ||
        strcmp(config.profiles[1].name, "release") != 0 ||
        strcmp(config.tests[1].name, "overflow") != 0 ||
        strcmp(basic->top, "counter_tb") != 0 ||
        strcmp(basic->waveform, "waves/basic.vcd") != 0 ||
        basic->kind != SOLAR_TEST_VERILOG ||
        !string_list_equals(
            &basic->sources,
            expected_basic_sources,
            sizeof(expected_basic_sources) / sizeof(expected_basic_sources[0])
        )) {
        failed = report_failure(test_name, "named profile/test content is incorrect");
    }

cleanup:
    solar_config_free(&config);
    return failed;
}

static int test_yanc_v2_and_generated_test(void)
{
    const char *test_name = "YANC format 2 manifest";
    const char *const expected_includes[] = {
        "src/include", "src/include with spaces"
    };
    SolarConfig config;
    SolarResult result;
    const SolarTest *generated;
    int failed;

    failed = evaluate_manifest(test_name, YANC_V2_MANIFEST, false, &config, &result);
    if (failed != 0) {
        solar_config_free(&config);
        return failed;
    }
    if (result.status != SOLAR_STATUS_OK) {
        failed = report_failure(test_name, result.diagnostic.message);
        goto cleanup;
    }
    if (config.manifest_format != 2U ||
        strcmp(config.project.language, "cmm") != 0 ||
        strcmp(config.compiler.backend, "yanc") != 0 ||
        strcmp(config.compiler.source, "src/processor.cmm") != 0 ||
        strcmp(config.compiler.processor, "processor") != 0 ||
        config.compiler.frequency_mhz != 100U ||
        config.compiler.simulation_clocks != 100000U ||
        !string_list_equals(
            &config.compiler.include_dirs,
            expected_includes,
            sizeof(expected_includes) / sizeof(expected_includes[0])
        ) ||
        strcmp(config.yanc.root, "/opt/YANC Toolchain") != 0 ||
        strcmp(config.yanc.diagnostics, "en") != 0) {
        failed = report_failure(test_name, "compiler or YANC settings are incorrect");
        goto cleanup;
    }

    generated = solar_config_find_test(&config, "generated");
    if (config.test_count != 1U || generated == NULL ||
        generated->kind != SOLAR_TEST_GENERATED ||
        strcmp(generated->top, "processor_tb") != 0) {
        failed = report_failure(test_name, "implicit generated test is missing");
    }

cleanup:
    solar_config_free(&config);
    return failed;
}

static int expect_config_error(
    const char *test_name,
    const char *manifest_text,
    const char *diagnostic_fragment
)
{
    SolarConfig config;
    SolarResult result;
    int failed = evaluate_manifest(
        test_name, manifest_text, true, &config, &result
    );

    if (failed == 0 &&
        (result.status != SOLAR_STATUS_CONFIG_ERROR ||
         result.diagnostic.message[0] == '\0' ||
         strstr(result.diagnostic.message, diagnostic_fragment) == NULL)) {
        failed = report_failure(test_name, "expected configuration error was not reported");
    }
    solar_config_free(&config);
    return failed;
}

static int test_rejected_format_and_duplicates(void)
{
    int failures = 0;

    failures += expect_config_error(
        "unsupported project format", UNSUPPORTED_FORMAT_MANIFEST, "format"
    );
    failures += expect_config_error(
        "zero project format", ZERO_FORMAT_MANIFEST, "format: 0"
    );
    failures += expect_config_error(
        "duplicate test name", DUPLICATE_TEST_MANIFEST, "same"
    );
    failures += expect_config_error(
        "duplicate profile name", DUPLICATE_PROFILE_MANIFEST, "debug"
    );
    failures += expect_config_error(
        "duplicate singleton table", DUPLICATE_TABLE_MANIFEST, "project"
    );
    failures += expect_config_error(
        "cocotb with Icarus backend",
        COCOTB_WRONG_BACKEND_MANIFEST,
        "requires the Verilator backend"
    );
    failures += expect_config_error(
        "cocotb invalid module name",
        COCOTB_INVALID_MODULE_MANIFEST,
        "invalid cocotb module path"
    );
    return failures;
}

static int test_v1_normalizes_to_default_test(void)
{
    const char *test_name = "format 1 normalization";
    const char *const expected_sources[] = {
        "tb/counter_tb.v", "tb/checker.v"
    };
    SolarConfig config;
    SolarResult result;
    const SolarTest *selected = NULL;
    int failed;

    failed = evaluate_manifest(test_name, V1_MANIFEST, false, &config, &result);
    if (failed != 0) {
        solar_config_free(&config);
        return failed;
    }
    if (result.status != SOLAR_STATUS_OK) {
        failed = report_failure(test_name, result.diagnostic.message);
        goto cleanup;
    }
    if (config.manifest_format != 1U ||
        config.project.default_test == NULL ||
        strcmp(config.project.default_test, "default") != 0 ||
        config.test_count != 1U || config.tests == NULL ||
        strcmp(config.tests[0].name, "default") != 0 ||
        strcmp(config.tests[0].top, "counter_tb") != 0 ||
        strcmp(config.tests[0].waveform, ".solar/build/sim/counter.vcd") != 0 ||
        config.tests[0].kind != SOLAR_TEST_VERILOG ||
        !string_list_equals(
            &config.tests[0].sources,
            expected_sources,
            sizeof(expected_sources) / sizeof(expected_sources[0])
        )) {
        failed = report_failure(test_name, "legacy fields were not normalized");
        goto cleanup;
    }

    result = solar_config_select_test(&config, NULL, &selected);
    if (result.status != SOLAR_STATUS_OK || selected != &config.tests[0]) {
        failed = report_failure(test_name, "implicit legacy test is not selectable");
    }

cleanup:
    solar_config_free(&config);
    return failed;
}

static int test_cocotb_test_fields(void)
{
    const char *test_name = "cocotb test fields";
    SolarConfig config;
    SolarResult result;
    int failed = evaluate_manifest(
        test_name, COCOTB_V2_MANIFEST, false, &config, &result
    );

    if (failed == 0 &&
        (result.status != SOLAR_STATUS_OK || config.test_count != 1U ||
         config.tests[0].driver == NULL ||
         strcmp(config.tests[0].driver, "cocotb") != 0 ||
         config.tests[0].cocotb == NULL ||
         strcmp(config.tests[0].cocotb, "tb/test_dut.py") != 0 ||
         config.tests[0].sources.count != 0U ||
         strcmp(config.tests[0].waveform, "dut.fst") != 0)) {
        failed = report_failure(test_name, "cocotb fields were not preserved");
    }
    solar_config_free(&config);
    return failed;
}

int main(void)
{
    int failures = 0;

    failures += test_verilog_v2_multiline_and_named_entries();
    failures += test_yanc_v2_and_generated_test();
    failures += test_rejected_format_and_duplicates();
    failures += test_v1_normalizes_to_default_test();
    failures += test_cocotb_test_fields();
    return failures == 0 ? 0 : 1;
}

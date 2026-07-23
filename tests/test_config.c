#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char VALID_MANIFEST[] =
    "[project]\n"
    "name = \"counter\"\n"
    "top = \"counter\"\n"
    "language = \"verilog\"\n"
    "\n"
    "[sources]\n"
    "rtl = [\"rtl/counter.v\", \"rtl/extra module.v\"]\n"
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

static int report_failure(const char *test_name, const char *message)
{
    (void)fprintf(stderr, "%s: %s\n", test_name, message);
    return 1;
}

static void remove_temporary_manifest(const char *manifest, const char *directory)
{
    if (manifest != NULL && unlink(manifest) != 0 && errno != ENOENT) {
        (void)fprintf(stderr, "could not remove temporary manifest %s\n", manifest);
    }
    if (directory != NULL && rmdir(directory) != 0 && errno != ENOENT) {
        (void)fprintf(stderr, "could not remove temporary directory %s\n", directory);
    }
}

static int test_valid_manifest(void)
{
    const char *test_name = "valid manifest";
    char directory_template[] = "/tmp/solar-config-valid-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *manifest = NULL;
    SolarConfig config;
    SolarResult result;
    int failed = 0;

    solar_config_init(&config);
    if (directory == NULL) {
        return report_failure(test_name, "mkdtemp failed");
    }
    manifest = join_path(directory, "solar.toml");
    if (manifest == NULL || write_text_file(manifest, VALID_MANIFEST) != 0) {
        failed = report_failure(test_name, "could not create the manifest");
        goto cleanup;
    }

    result = solar_config_parse_file(manifest, &config);
    if (result.status != SOLAR_STATUS_OK) {
        failed = report_failure(test_name, result.diagnostic.message);
        goto cleanup;
    }
    result = solar_config_validate(&config, manifest);
    if (result.status != SOLAR_STATUS_OK) {
        failed = report_failure(test_name, result.diagnostic.message);
        goto cleanup;
    }
    if (strcmp(config.project.name, "counter") != 0 ||
        strcmp(config.project.top, "counter") != 0 ||
        strcmp(config.project.language, "verilog") != 0) {
        failed = report_failure(test_name, "project strings were parsed incorrectly");
        goto cleanup;
    }
    if (config.sources.rtl.count != 2U ||
        strcmp(config.sources.rtl.items[0], "rtl/counter.v") != 0 ||
        strcmp(config.sources.rtl.items[1], "rtl/extra module.v") != 0 ||
        config.sources.testbench.count != 2U ||
        strcmp(config.sources.testbench.items[0], "tb/counter_tb.v") != 0 ||
        strcmp(config.sources.testbench.items[1], "tb/checker.v") != 0) {
        failed = report_failure(test_name, "source lists lost their values or order");
        goto cleanup;
    }
    if (strcmp(config.simulation.backend, "iverilog") != 0 ||
        strcmp(config.simulation.top, "counter_tb") != 0 ||
        strcmp(config.simulation.waveform, ".solar/build/sim/counter.vcd") != 0 ||
        strcmp(config.synthesis.backend, "yosys") != 0 ||
        strcmp(config.synthesis.top, "counter") != 0) {
        failed = report_failure(test_name, "tool settings were parsed incorrectly");
    }

cleanup:
    solar_config_free(&config);
    remove_temporary_manifest(manifest, directory);
    free(manifest);
    return failed;
}

static int test_invalid_syntax(void)
{
    const char *test_name = "invalid syntax";
    const char invalid_manifest[] =
        "[project]\n"
        "name = \"counter\"\n"
        "[sources]\n"
        "rtl = [\"rtl/counter.v\"\n";
    char directory_template[] = "/tmp/solar-config-syntax-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *manifest = NULL;
    SolarConfig config;
    SolarResult result;
    int failed = 0;

    solar_config_init(&config);
    if (directory == NULL) {
        return report_failure(test_name, "mkdtemp failed");
    }
    manifest = join_path(directory, "solar.toml");
    if (manifest == NULL || write_text_file(manifest, invalid_manifest) != 0) {
        failed = report_failure(test_name, "could not create the manifest");
        goto cleanup;
    }

    result = solar_config_parse_file(manifest, &config);
    if (result.status != SOLAR_STATUS_CONFIG_ERROR ||
        strstr(result.diagnostic.message, manifest) == NULL ||
        strstr(result.diagnostic.message, "[sources].rtl") == NULL ||
        result.diagnostic.hint[0] == '\0') {
        failed = report_failure(
            test_name,
            "syntax error did not return an actionable config diagnostic"
        );
    }

cleanup:
    solar_config_free(&config);
    remove_temporary_manifest(manifest, directory);
    free(manifest);
    return failed;
}

static int test_missing_required_key(void)
{
    const char *test_name = "missing required key";
    const char missing_top_manifest[] =
        "[project]\n"
        "name = \"counter\"\n"
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
    char directory_template[] = "/tmp/solar-config-required-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *manifest = NULL;
    SolarConfig config;
    SolarResult result;
    int failed = 0;

    solar_config_init(&config);
    if (directory == NULL) {
        return report_failure(test_name, "mkdtemp failed");
    }
    manifest = join_path(directory, "solar.toml");
    if (manifest == NULL || write_text_file(manifest, missing_top_manifest) != 0) {
        failed = report_failure(test_name, "could not create the manifest");
        goto cleanup;
    }

    result = solar_config_parse_file(manifest, &config);
    if (result.status != SOLAR_STATUS_OK) {
        failed = report_failure(test_name, "parse should succeed before validation");
        goto cleanup;
    }
    result = solar_config_validate(&config, manifest);
    if (result.status != SOLAR_STATUS_CONFIG_ERROR ||
        strstr(result.diagnostic.message, "[project].top") == NULL ||
        strstr(result.diagnostic.hint, "top = \"counter\"") == NULL) {
        failed = report_failure(test_name, "missing top diagnostic was not actionable");
    }

cleanup:
    solar_config_free(&config);
    remove_temporary_manifest(manifest, directory);
    free(manifest);
    return failed;
}

static int test_unsafe_source_path(void)
{
    const char *test_name = "unsafe source path";
    const char unsafe_manifest[] =
        "[project]\n"
        "name = \"counter\"\n"
        "top = \"counter\"\n"
        "language = \"verilog\"\n"
        "[sources]\n"
        "rtl = [\"rtl/../outside.v\"]\n"
        "testbench = [\"tb/counter_tb.v\"]\n"
        "[simulation]\n"
        "backend = \"iverilog\"\n"
        "top = \"counter_tb\"\n"
        "waveform = \".solar/build/sim/counter.vcd\"\n"
        "[synthesis]\n"
        "backend = \"yosys\"\n"
        "top = \"counter\"\n";
    char directory_template[] = "/tmp/solar-config-path-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *manifest = NULL;
    SolarConfig config;
    SolarResult result;
    int failed = 0;

    solar_config_init(&config);
    if (directory == NULL) {
        return report_failure(test_name, "mkdtemp failed");
    }
    manifest = join_path(directory, "solar.toml");
    if (manifest == NULL || write_text_file(manifest, unsafe_manifest) != 0) {
        failed = report_failure(test_name, "could not create the manifest");
        goto cleanup;
    }

    result = solar_config_parse_file(manifest, &config);
    if (result.status != SOLAR_STATUS_OK) {
        failed = report_failure(test_name, "unsafe paths are a semantic error");
        goto cleanup;
    }
    result = solar_config_validate(&config, manifest);
    if (result.status != SOLAR_STATUS_CONFIG_ERROR ||
        strstr(result.diagnostic.message, "[sources].rtl[0]") == NULL ||
        strstr(result.diagnostic.message, "rtl/../outside.v") == NULL) {
        failed = report_failure(test_name, "unsafe path was not rejected with context");
    }

cleanup:
    solar_config_free(&config);
    remove_temporary_manifest(manifest, directory);
    free(manifest);
    return failed;
}

static int expect_semantic_error(
    const char *test_name,
    const char *manifest_text,
    const char *diagnostic_fragment
)
{
    char directory_template[] = "/tmp/solar-config-semantic-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *manifest = NULL;
    SolarConfig config;
    SolarResult result;
    int failed = 0;

    solar_config_init(&config);
    if (directory == NULL) return report_failure(test_name, "mkdtemp failed");
    manifest = join_path(directory, "solar.toml");
    if (manifest == NULL || write_text_file(manifest, manifest_text) != 0) {
        failed = report_failure(test_name, "could not create semantic fixture");
        goto cleanup;
    }
    result = solar_config_parse_file(manifest, &config);
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_config_validate(&config, manifest);
    }
    if (result.status != SOLAR_STATUS_CONFIG_ERROR ||
        strstr(result.diagnostic.message, diagnostic_fragment) == NULL) {
        failed = report_failure(test_name, "unsafe semantic value was accepted");
    }

cleanup:
    solar_config_free(&config);
    remove_temporary_manifest(manifest, directory);
    free(manifest);
    return failed;
}

static int test_semantic_safety_values(void)
{
    const char reserved_source[] =
        "[project]\nname = \"counter\"\ntop = \"counter\"\nlanguage = \"verilog\"\n"
        "[sources]\nrtl = [\".solar/source.v\"]\ntestbench = [\"tb/counter_tb.v\"]\n"
        "[simulation]\nbackend = \"iverilog\"\ntop = \"counter_tb\"\n"
        "waveform = \".solar/build/sim/counter.vcd\"\n"
        "[synthesis]\nbackend = \"yosys\"\ntop = \"counter\"\n";
    const char injected_top[] =
        "[project]\nname = \"counter\"\ntop = \"counter\"\nlanguage = \"verilog\"\n"
        "[sources]\nrtl = [\"rtl/counter.v\"]\ntestbench = [\"tb/counter_tb.v\"]\n"
        "[simulation]\nbackend = \"iverilog\"\ntop = \"counter_tb\"\n"
        "waveform = \".solar/build/sim/counter.vcd\"\n"
        "[synthesis]\nbackend = \"yosys\"\ntop = \"counter; write_verilog outside.v\"\n";
    const char control_name[] =
        "[project]\nname = \"bad\\nname\"\ntop = \"counter\"\nlanguage = \"verilog\"\n"
        "[sources]\nrtl = [\"rtl/counter.v\"]\ntestbench = [\"tb/counter_tb.v\"]\n"
        "[simulation]\nbackend = \"iverilog\"\ntop = \"counter_tb\"\n"
        "waveform = \".solar/build/sim/counter.vcd\"\n"
        "[synthesis]\nbackend = \"yosys\"\ntop = \"counter\"\n";
    int failures = 0;

    failures += expect_semantic_error(
        "reserved source directory", reserved_source, "reserved generated-data"
    );
    failures += expect_semantic_error(
        "synthesis top injection", injected_top, "[synthesis].top"
    );
    failures += expect_semantic_error(
        "project name control character", control_name, "[project].name"
    );
    return failures;
}

static int test_oversized_manifest_is_rejected(void)
{
    const char *test_name = "oversized manifest";
    char directory_template[] = "/tmp/solar-config-large-XXXXXX";
    char *directory = mkdtemp(directory_template);
    char *manifest = NULL;
    SolarConfig config;
    SolarResult result;
    int failed = 0;

    solar_config_init(&config);
    if (directory == NULL) return report_failure(test_name, "mkdtemp failed");
    manifest = join_path(directory, "solar.toml");
    if (manifest == NULL || write_text_file(manifest, "[solar]\nformat = 2\n") != 0 ||
        truncate(manifest, (off_t)(17U * 1024U * 1024U)) != 0) {
        failed = report_failure(test_name, "could not create sparse manifest");
        goto cleanup;
    }
    result = solar_config_parse_file(manifest, &config);
    if (result.status != SOLAR_STATUS_CONFIG_ERROR ||
        strstr(result.diagnostic.message, "16 MiB") == NULL) {
        failed = report_failure(test_name, "oversized manifest was accepted");
    }

cleanup:
    solar_config_free(&config);
    remove_temporary_manifest(manifest, directory);
    free(manifest);
    return failed;
}

int main(void)
{
    int failures = 0;

    failures += test_valid_manifest();
    failures += test_invalid_syntax();
    failures += test_missing_required_key();
    failures += test_unsafe_source_path();
    failures += test_semantic_safety_values();
    failures += test_oversized_manifest_is_rejected();
    return failures == 0 ? 0 : 1;
}

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/project.h"

#include <errno.h>
#include <ftw.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    const char *name;
    const char *default_test;
    const char *default_profile;
    const char *profile_name;
    const char *test_name;
    const char *define_value;
    const char *include_path;
    const char *waveform;
    const char *expected_message;
} VerilogValidationCase;

typedef struct {
    const char *name;
    const char *language;
    const char *source_path;
    const char *processor;
    unsigned long frequency_mhz;
    unsigned long simulation_clocks;
    bool create_source;
    const char *expected_message;
} YancValidationCase;

static int report_failure(const char *test_name, const char *message)
{
    (void)fprintf(stderr, "%s: %s\n", test_name, message);
    return 1;
}

static char *join_path(const char *root, const char *relative)
{
    size_t capacity = strlen(root) + strlen(relative) + 2U;
    char *path = malloc(capacity);

    if (path != NULL) {
        (void)snprintf(path, capacity, "%s/%s", root, relative);
    }
    return path;
}

static int create_directory(const char *root, const char *relative)
{
    char *path = join_path(root, relative);
    int status;

    if (path == NULL) return -1;
    status = mkdir(path, 0700);
    free(path);
    return status;
}

static int write_text_file(
    const char *root,
    const char *relative,
    const char *contents
)
{
    char *path = join_path(root, relative);
    FILE *file;
    int failed = 0;

    if (path == NULL) return -1;
    file = fopen(path, "w");
    if (file == NULL) {
        free(path);
        return -1;
    }
    if (fputs(contents, file) == EOF) failed = 1;
    if (fclose(file) != 0) failed = 1;
    free(path);
    return failed == 0 ? 0 : -1;
}

static int remove_tree_entry(
    const char *path,
    const struct stat *information,
    int type,
    struct FTW *walk
)
{
    (void)information;
    (void)type;
    (void)walk;
    return remove(path);
}

static int remove_tree(const char *root)
{
    if (root == NULL || root[0] == '\0') return 0;
    /* Remove children first and never follow a fixture symlink. */
    if (nftw(root, remove_tree_entry, 32, FTW_DEPTH | FTW_PHYS) == 0) {
        return 0;
    }
    return errno == ENOENT ? 0 : -1;
}

static int prepare_verilog_files(const char *root)
{
    if (create_directory(root, "rtl") != 0 ||
        create_directory(root, "tb") != 0 ||
        create_directory(root, "include") != 0 ||
        write_text_file(
            root, "rtl/design.v", "module design; endmodule\n"
        ) != 0 ||
        write_text_file(
            root, "tb/basic_tb.v", "module basic_tb; endmodule\n"
        ) != 0 ||
        write_text_file(root, "include-file", "not a directory\n") != 0) {
        return -1;
    }
    return 0;
}

static int write_verilog_manifest(
    const char *root,
    const VerilogValidationCase *validation_case
)
{
    char *path = join_path(root, "solar.toml");
    FILE *file;
    int failed = 0;

    if (path == NULL) return -1;
    file = fopen(path, "w");
    free(path);
    if (file == NULL) return -1;
    if (fprintf(
            file,
            "[solar]\n"
            "format = 2\n"
            "\n"
            "[project]\n"
            "name = \"validation\"\n"
            "language = \"verilog\"\n"
            "default_test = \"%s\"\n"
            "default_profile = \"%s\"\n"
            "\n"
            "[sources]\n"
            "rtl = [\"rtl/design.v\"]\n"
            "include_dirs = [\"%s\"]\n"
            "defines = [\"%s\"]\n"
            "\n"
            "[simulation]\n"
            "backend = \"iverilog\"\n"
            "\n"
            "[synthesis]\n"
            "backend = \"yosys\"\n"
            "top = \"design\"\n"
            "\n"
            "[[profile]]\n"
            "name = \"%s\"\n"
            "\n"
            "[[test]]\n"
            "name = \"%s\"\n"
            "top = \"basic_tb\"\n"
            "sources = [\"tb/basic_tb.v\"]\n"
            "waveform = \"%s\"\n",
            validation_case->default_test,
            validation_case->default_profile,
            validation_case->include_path,
            validation_case->define_value,
            validation_case->profile_name,
            validation_case->test_name,
            validation_case->waveform
        ) < 0) {
        failed = 1;
    }
    if (fclose(file) != 0) failed = 1;
    return failed == 0 ? 0 : -1;
}

static int prepare_yanc_files(
    const char *root,
    const YancValidationCase *validation_case
)
{
    if (create_directory(root, "src") != 0) return -1;
    if (validation_case->create_source &&
        write_text_file(
            root, validation_case->source_path, "source placeholder\n"
        ) != 0) {
        return -1;
    }
    return 0;
}

static int write_yanc_manifest(
    const char *root,
    const YancValidationCase *validation_case
)
{
    char *path = join_path(root, "solar.toml");
    FILE *file;
    int failed = 0;

    if (path == NULL) return -1;
    file = fopen(path, "w");
    free(path);
    if (file == NULL) return -1;
    if (fprintf(
            file,
            "[solar]\n"
            "format = 2\n"
            "\n"
            "[project]\n"
            "name = \"yanc-validation\"\n"
            "language = \"%s\"\n"
            "default_test = \"generated\"\n"
            "\n"
            "[compiler]\n"
            "backend = \"yanc\"\n"
            "source = \"%s\"\n"
            "processor = \"%s\"\n"
            "frequency_mhz = %lu\n"
            "simulation_clocks = %lu\n"
            "\n"
            "[yanc]\n"
            "diagnostics = \"pt\"\n"
            "\n"
            "[simulation]\n"
            "backend = \"iverilog\"\n"
            "\n"
            "[synthesis]\n"
            "backend = \"yosys\"\n"
            "top = \"sapho_demo\"\n",
            validation_case->language,
            validation_case->source_path,
            validation_case->processor,
            validation_case->frequency_mhz,
            validation_case->simulation_clocks
        ) < 0) {
        failed = 1;
    }
    if (fclose(file) != 0) failed = 1;
    return failed == 0 ? 0 : -1;
}

static int expect_validation_error(
    const char *root,
    const char *test_name,
    const char *expected_message
)
{
    SolarProject project;
    SolarResult result;
    int failed = 0;

    solar_project_init(&project);
    result = solar_project_load(root, &project);
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_project_validate(&project);
    }
    if (result.status != SOLAR_STATUS_CONFIG_ERROR) {
        (void)fprintf(
            stderr,
            "%s: expected configuration error, got %s: %s\n",
            test_name,
            solar_status_name(result.status),
            result.diagnostic.message
        );
        failed = 1;
    } else if (strstr(result.diagnostic.message, expected_message) == NULL) {
        (void)fprintf(
            stderr,
            "%s: diagnostic did not contain '%s': %s\n",
            test_name,
            expected_message,
            result.diagnostic.message
        );
        failed = 1;
    } else if (result.diagnostic.hint[0] == '\0') {
        failed = report_failure(test_name, "validation error had no corrective hint");
    }
    solar_project_free(&project);
    return failed;
}

static int run_verilog_case(const VerilogValidationCase *validation_case)
{
    char directory_template[] = "/tmp/solar-validation-v2-verilog-XXXXXX";
    char *root = mkdtemp(directory_template);
    int failed = 0;

    if (root == NULL) return report_failure(validation_case->name, "mkdtemp failed");
    if (prepare_verilog_files(root) != 0 ||
        write_verilog_manifest(root, validation_case) != 0) {
        failed = report_failure(
            validation_case->name, "could not prepare Verilog fixture"
        );
    } else {
        failed = expect_validation_error(
            root, validation_case->name, validation_case->expected_message
        );
    }
    if (remove_tree(root) != 0) {
        failed += report_failure(
            validation_case->name, "could not remove temporary Verilog fixture"
        );
    }
    return failed;
}

static int run_yanc_case(const YancValidationCase *validation_case)
{
    char directory_template[] = "/tmp/solar-validation-v2-yanc-XXXXXX";
    char *root = mkdtemp(directory_template);
    int failed = 0;

    if (root == NULL) return report_failure(validation_case->name, "mkdtemp failed");
    if (prepare_yanc_files(root, validation_case) != 0 ||
        write_yanc_manifest(root, validation_case) != 0) {
        failed = report_failure(
            validation_case->name, "could not prepare YANC fixture"
        );
    } else {
        failed = expect_validation_error(
            root, validation_case->name, validation_case->expected_message
        );
    }
    if (remove_tree(root) != 0) {
        failed += report_failure(
            validation_case->name, "could not remove temporary YANC fixture"
        );
    }
    return failed;
}

int main(void)
{
    const VerilogValidationCase verilog_cases[] = {
        {
            "missing default test", "missing", "debug", "debug", "basic",
            "VALID", "include", "basic.vcd",
            "[project].default_test does not exist"
        },
        {
            "missing default profile", "basic", "missing", "debug", "basic",
            "VALID", "include", "basic.vcd",
            "[project].default_profile does not exist"
        },
        {
            "unsafe test name", "bad/name", "debug", "debug", "bad/name",
            "VALID", "include", "basic.vcd", "test has an unsafe name"
        },
        {
            "unsafe profile name", "basic", ".debug", ".debug", "basic",
            "VALID", "include", "basic.vcd", "profile has an unsafe name"
        },
        {
            "empty define", "basic", "debug", "debug", "basic", "",
            "include", "basic.vcd", "not a valid Verilog macro"
        },
        {
            "define begins with hyphen", "basic", "debug", "debug", "basic",
            "-DEBUG", "include", "basic.vcd", "not a valid Verilog macro"
        },
        {
            "invalid macro name", "basic", "debug", "debug", "basic",
            "9DEBUG", "include", "basic.vcd", "not a valid Verilog macro"
        },
        {
            "missing include directory", "basic", "debug", "debug", "basic",
            "VALID", "missing", "basic.vcd",
            "does not name a readable directory"
        },
        {
            "include is not a directory", "basic", "debug", "debug", "basic",
            "VALID", "include-file", "basic.vcd",
            "does not name a readable directory"
        },
        {
            "absolute waveform", "basic", "debug", "debug", "basic",
            "VALID", "include", "/tmp/basic.vcd", "unsafe waveform"
        },
        {
            "waveform parent traversal", "basic", "debug", "debug", "basic",
            "VALID", "include", "waves/../basic.vcd", "unsafe waveform"
        }
    };
    const YancValidationCase yanc_cases[] = {
        {
            "unsafe processor", "cmm", "src/program.cmm", "9processor",
            100UL, 100000UL, true, "contains an invalid processor name"
        },
        {
            "processor name too long", "cmm", "src/program.cmm",
            "processor_name_that_is_intentionally_longer_than_the_127_character_limit_for_the_fixed_buffers_in_the_bundled_yanc_toolchain_1234567890",
            100UL, 100000UL, true, "contains an invalid processor name"
        },
        {
            "zero YANC frequency", "cmm", "src/program.cmm", "sapho_demo",
            0UL, 100000UL, true, "frequency_mhz must be positive"
        },
        {
            "zero YANC simulation clocks", "cmm", "src/program.cmm",
            "sapho_demo", 100UL, 0UL, true,
            "simulation_clocks must be positive"
        },
        {
            "missing YANC source", "cmm", "src/missing.cmm", "sapho_demo",
            100UL, 100000UL, false, "does not name a readable file"
        },
        {
            "CMM incompatible source extension", "cmm", "src/program.cpp",
            "sapho_demo", 100UL, 100000UL, true,
            "extension is incompatible with language cmm"
        },
        {
            "C++ incompatible source extension", "cpp", "src/program.cmm",
            "sapho_demo", 100UL, 100000UL, true,
            "extension is incompatible with language cpp"
        },
        {
            "Assembly incompatible source extension", "asm", "src/program.cpp",
            "sapho_demo", 100UL, 100000UL, true,
            "extension is incompatible with language asm"
        }
    };
    size_t index;
    int failures = 0;

    for (index = 0U;
         index < sizeof(verilog_cases) / sizeof(verilog_cases[0]);
         index++) {
        failures += run_verilog_case(&verilog_cases[index]);
    }
    for (index = 0U;
         index < sizeof(yanc_cases) / sizeof(yanc_cases[0]);
         index++) {
        failures += run_yanc_case(&yanc_cases[index]);
    }
    if (failures == 0) {
        (void)printf("format-2 validation tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "solar/filesystem.h"
#include "solar/project.h"
#include "solar/runner.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *join_path(const char *left, const char *right)
{
    size_t length = strlen(left) + strlen(right) + 2U;
    char *path = malloc(length);

    if (path != NULL) (void)snprintf(path, length, "%s/%s", left, right);
    return path;
}

static bool regular_file(const char *root, const char *relative)
{
    char *path = join_path(root, relative);
    struct stat information;
    bool exists = path != NULL && stat(path, &information) == 0 &&
        S_ISREG(information.st_mode);

    free(path);
    return exists;
}

static char *read_text_file(const char *path)
{
    FILE *file = fopen(path, "r");
    long length;
    char *text;

    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) (void)fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length < 0L || fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return NULL;
    }
    text = malloc((size_t)length + 1U);
    if (text == NULL) {
        (void)fclose(file);
        return NULL;
    }
    if (fread(text, 1U, (size_t)length, file) != (size_t)length) {
        free(text);
        (void)fclose(file);
        return NULL;
    }
    text[length] = '\0';
    if (fclose(file) != 0) {
        free(text);
        return NULL;
    }
    return text;
}

static int run_solar_arguments(
    const char *solar,
    const char *root,
    const char *const arguments[],
    const char *description,
    const char *expected_stdout
)
{
    SolarProcessSpec specification = {
        solar, arguments, root, NULL, NULL, NULL, 0U, NULL
    };
    SolarProcessResult process;
    SolarResult result;
    bool expected_output_missing;
    int code;

    solar_process_result_init(&process);
    result = solar_runner_run(&specification, &process);
    code = process.outcome == SOLAR_PROCESS_EXITED
        ? process.exit_code
        : 1;
    expected_output_missing = expected_stdout != NULL &&
        (process.stdout_text == NULL ||
         strstr(process.stdout_text, expected_stdout) == NULL);
    if (result.status != SOLAR_STATUS_OK || code != 0 ||
        expected_output_missing) {
        (void)fprintf(
            stderr,
            "YANC integration %s failed: %s\nstdout: %s\nstderr: %s\n",
            description,
            result.diagnostic.message,
            process.stdout_text == NULL ? "" : process.stdout_text,
            process.stderr_text == NULL ? "" : process.stderr_text
        );
        if (code == 0) code = 1;
    }
    solar_process_result_free(&process);
    return code;
}

static int run_solar_command(
    const char *solar,
    const char *root,
    const char *command
)
{
    const char *arguments[] = {solar, command, NULL};

    return run_solar_arguments(
        solar, root, arguments, command, NULL
    );
}

static int write_repeated_identifier(FILE *file)
{
    size_t index;

    for (index = 0U; index < 700U; index++) {
        if (fputc('x', file) == EOF) return -1;
    }
    return 0;
}

static int write_long_identifier_source(
    const char *path,
    const char *template_name
)
{
    FILE *file = fopen(path, "w");
    int failed = 0;

    if (file == NULL) return -1;
    if (strcmp(template_name, "yanc-cpp") == 0) {
        if (fputs(
                "#pragma yanc prname sapho_demo\n"
                "void main(void) { int ", file) == EOF ||
            write_repeated_identifier(file) != 0 ||
            fputs(" = 0; out(0, 0); }\n", file) == EOF) {
            failed = 1;
        }
    } else {
        if (fputs(
                "#PRNAME sapho_demo\n#NUBITS 16\n#NDSTAC 4\n#SDEPTH 4\n"
                "#NUIOIN 1\n#NUIOOU 1\n#NBMANT 10\n#NBEXPO 5\n#NUGAIN 128\n",
                file) == EOF) {
            failed = 1;
        }
        if (!failed && strcmp(template_name, "yanc-cmm") == 0) {
            if (fputs("void main() { int ", file) == EOF ||
                write_repeated_identifier(file) != 0 ||
                fputs("; while (1) { out(0, 0); } }\n", file) == EOF) {
                failed = 1;
            }
        } else if (!failed) {
            if (fputs("JMP main\n@main NOP\nLOD 1\nSET ", file) == EOF ||
                write_repeated_identifier(file) != 0 ||
                fputs("\n@fim JMP fim\n", file) == EOF) {
                failed = 1;
            }
        }
    }
    if (fclose(file) != 0) failed = 1;
    return failed ? -1 : 0;
}

static int verify_yanc_identifier_limit(
    const char *solar,
    const char *root,
    const char *template_name
)
{
    const char *arguments[] = {solar, "build", "rtl", NULL};
    const char *source_relative = strcmp(template_name, "yanc-cmm") == 0
        ? "software/processor.cmm"
        : (strcmp(template_name, "yanc-cpp") == 0
            ? "software/processor.cpp" : "software/processor.asm");
    const char *stage = strcmp(template_name, "yanc-cmm") == 0
        ? "cmmcomp" : "appcomp";
    char *source_path = join_path(root, source_relative);
    char *hardware_path = join_path(root, "hardware/sapho_demo.v");
    char log_relative[128];
    char *log_path;
    char *hardware_before = NULL;
    char *hardware_after = NULL;
    char *log_text = NULL;
    SolarProcessSpec specification = {
        solar, arguments, root, NULL, NULL, NULL, 0U, NULL
    };
    SolarProcessResult process;
    SolarResult result;
    int failed = 0;

    (void)snprintf(
        log_relative, sizeof(log_relative),
        ".solar/logs/yanc/sapho_demo/%s.stderr.log", stage
    );
    log_path = join_path(root, log_relative);
    solar_process_result_init(&process);
    if (source_path == NULL || hardware_path == NULL || log_path == NULL ||
        (hardware_before = read_text_file(hardware_path)) == NULL ||
        write_long_identifier_source(source_path, template_name) != 0) {
        failed = 1;
        goto cleanup;
    }
    result = solar_runner_run(&specification, &process);
    log_text = read_text_file(log_path);
    hardware_after = read_text_file(hardware_path);
    if (result.status != SOLAR_STATUS_PROCESS_FAILED ||
        process.outcome != SOLAR_PROCESS_EXITED || process.exit_code == 0 ||
        log_text == NULL || strstr(log_text, "limite seguro de 63 bytes") == NULL ||
        strstr(log_text, "AddressSanitizer") != NULL || hardware_after == NULL ||
        strcmp(hardware_before, hardware_after) != 0) {
        (void)fprintf(
            stderr,
            "YANC %s long-identifier guard failed\nstdout: %s\nstderr: %s\nlog: %s\n",
            template_name,
            process.stdout_text == NULL ? "" : process.stdout_text,
            process.stderr_text == NULL ? "" : process.stderr_text,
            log_text == NULL ? "" : log_text
        );
        failed = 1;
    }

cleanup:
    solar_process_result_free(&process);
    free(log_text);
    free(hardware_after);
    free(hardware_before);
    free(log_path);
    free(hardware_path);
    free(source_path);
    return failed;
}

static void remove_template_project(const char *root, const char *template_name)
{
    const char *source = strcmp(template_name, "yanc-cmm") == 0
        ? "software/processor.cmm"
        : (strcmp(template_name, "yanc-cpp") == 0
            ? "software/processor.cpp"
            : "software/processor.asm");
    bool removed = false;
    char *path;

    (void)solar_filesystem_clean_project(root, &removed);
    path = join_path(root, source);
    if (path != NULL) (void)unlink(path);
    free(path);
    if (strcmp(template_name, "yanc-cpp") == 0) {
        path = join_path(root, "software/include/solar_math.hpp");
        if (path != NULL) (void)unlink(path);
        free(path);
        path = join_path(root, "software/include");
        if (path != NULL) (void)rmdir(path);
        free(path);
    }
    path = join_path(root, "software");
    if (path != NULL) (void)rmdir(path);
    free(path);
    path = join_path(root, "solar.toml");
    if (path != NULL) (void)unlink(path);
    free(path);
    (void)rmdir(root);
}

static int verify_artifacts(const char *root, const char *template_name)
{
    static const char *const ARTIFACTS[] = {
        ".solar/tmp/yanc/sapho_demo/publish/software/sapho_demo.asm",
        "hardware/sapho_demo.v",
        "hardware/sapho_demo_data.mif",
        "hardware/sapho_demo_inst.mif",
        "simulation/sapho_demo_tb.v",
        ".solar/state/yanc/sapho_demo/build-info.txt",
        ".solar/tmp/tests/generated/simulation.vvp",
        ".solar/tmp/tests/generated/output_0.txt",
        "simulation/sapho_demo_tb.vcd",
        ".solar/tmp/synth/synthesis.ys",
        "hardware/sapho_demo_netlist.v",
        "hardware/statistics.txt"
    };
    size_t index;

    for (index = 0U; index < sizeof(ARTIFACTS) / sizeof(ARTIFACTS[0]); index++) {
        if (!regular_file(root, ARTIFACTS[index])) {
            (void)fprintf(
                stderr, "missing YANC integration artifact: %s\n",
                ARTIFACTS[index]
            );
            return 1;
        }
    }
    if (strcmp(template_name, "yanc-asm") == 0) {
        if (regular_file(root, "software/sapho_demo.asm")) {
            (void)fprintf(
                stderr, "Solar published a generated copy over Assembly sources\n"
            );
            return 1;
        }
    } else if (!regular_file(root, "software/sapho_demo.asm")) {
        (void)fprintf(
            stderr, "missing public generated Assembly: software/sapho_demo.asm\n"
        );
        return 1;
    }
    if (regular_file(root, "simulation/output_0.txt")) {
        (void)fprintf(
            stderr,
            "YANC testbench wrote an unregistered sidecar into simulation/\n"
        );
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *test_root = getenv("SOLAR_TEST_YANC_ROOT");
    char directory_template[] = "/tmp/solar yanc real-XXXXXX";
    char *directory;
    char *solar;
    char *assembly_source_path = NULL;
    char *assembly_before = NULL;
    char *assembly_after = NULL;
    SolarResult result;
    int command_code;
    int return_code = 1;

    if (argc != 3) {
        (void)fprintf(
            stderr, "expected solar executable and YANC template name\n"
        );
        return 1;
    }
    if (test_root == NULL || test_root[0] == '\0') {
        (void)printf(
            "SKIP: SOLAR_TEST_YANC_ROOT is not defined; real YANC %s flow was not run\n",
            argv[2]
        );
        return 77;
    }
    if (setenv("SOLAR_YANC_ROOT", test_root, 1) != 0) return 1;
    solar = realpath(argv[1], NULL);
    if (solar == NULL) return 1;
    directory = mkdtemp(directory_template);
    if (directory == NULL) {
        free(solar);
        return 1;
    }
    result = solar_project_initialize_template(directory, argv[2]);
    if (result.status != SOLAR_STATUS_OK) {
        (void)fprintf(
            stderr, "YANC integration init: %s\n",
            result.diagnostic.message
        );
        goto cleanup;
    }
    if (strcmp(argv[2], "yanc-asm") == 0) {
        assembly_source_path = join_path(directory, "software/processor.asm");
        if (assembly_source_path == NULL ||
            (assembly_before = read_text_file(assembly_source_path)) == NULL) {
            (void)fprintf(stderr, "could not snapshot Assembly source\n");
            goto cleanup;
        }
    }
    command_code = run_solar_command(solar, directory, "check");
    if (command_code != 0) goto cleanup;
    {
        const char *arguments[] = {
            solar, "build", "sim", "--list", NULL
        };

        command_code = run_solar_arguments(
            solar, directory, arguments, "build sim --list",
            "generated    top: sapho_demo_tb [YANC] [default]"
        );
    }
    if (command_code != 0) goto cleanup;
    {
        const char *arguments[] = {solar, "build", "rtl", NULL};

        command_code = run_solar_arguments(
            solar, directory, arguments, "build rtl", "PASS  rtl"
        );
    }
    if (command_code == 4) {
        return_code = 77;
        goto cleanup;
    }
    if (command_code != 0) goto cleanup;
    {
        const char *arguments[] = {
            solar, "build", "sim", "generated", "--profile", "debug", NULL
        };

        command_code = run_solar_arguments(
            solar, directory, arguments, "build sim generated --profile debug",
            "PASS  generated"
        );
    }
    if (command_code == 4) {
        (void)printf("SKIP: Icarus Verilog or VVP is unavailable\n");
        return_code = 77;
        goto cleanup;
    }
    if (command_code != 0) goto cleanup;
    if (strcmp(argv[2], "yanc-cmm") == 0) {
        const char *arguments[] = {
            solar, "build", "sim", "generated", "--profile", "debug", NULL
        };

        command_code = run_solar_arguments(
            solar, directory, arguments,
            "build sim generated --profile debug", "PASS  generated"
        );
        if (command_code != 0) goto cleanup;
    }
    {
        const char *arguments[] = {
            solar, "build", "synth", "--profile", "debug", NULL
        };

        command_code = run_solar_arguments(
            solar, directory, arguments, "build synth --profile debug",
            "Netlist:"
        );
    }
    if (command_code == 4) {
        (void)printf("SKIP: Yosys is unavailable\n");
        return_code = 77;
        goto cleanup;
    }
    if (command_code != 0) goto cleanup;
    return_code = verify_artifacts(directory, argv[2]);
    if (return_code == 0 && assembly_source_path != NULL) {
        assembly_after = read_text_file(assembly_source_path);
        if (assembly_after == NULL ||
            strcmp(assembly_before, assembly_after) != 0) {
            (void)fprintf(stderr, "Solar modified the original Assembly source\n");
            return_code = 1;
        }
    }
    if (return_code == 0) {
        return_code = verify_yanc_identifier_limit(solar, directory, argv[2]);
    }

cleanup:
    remove_template_project(directory, argv[2]);
    free(assembly_after);
    free(assembly_before);
    free(assembly_source_path);
    free(solar);
    return return_code;
}

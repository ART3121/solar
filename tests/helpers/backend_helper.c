#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *program_name(const char *path)
{
    const char *separator = strrchr(path, '/');

    return separator == NULL ? path : separator + 1;
}

static int write_file(const char *path, const char *content)
{
    FILE *file = fopen(path, "w");
    int failed;

    if (file == NULL) return -1;
    failed = fputs(content, file) == EOF;
    if (fclose(file) != 0) failed = 1;
    return failed ? -1 : 0;
}

static char *current_directory(void)
{
    size_t capacity = 128U;

    for (;;) {
        char *directory = malloc(capacity);

        if (directory == NULL) return NULL;
        if (getcwd(directory, capacity) != NULL) return directory;
        free(directory);
        if (errno != ERANGE || capacity > SIZE_MAX / 2U) return NULL;
        capacity *= 2U;
    }
}

static int record_invocation(const char *tool, int argc, char **argv)
{
    const char *record_path = getenv("SOLAR_BACKEND_RECORD");
    char *directory = current_directory();
    FILE *record;
    int index;
    int failed = 0;

    if (record_path == NULL || directory == NULL) {
        free(directory);
        return -1;
    }
    record = fopen(record_path, "a");
    if (record == NULL) {
        free(directory);
        return -1;
    }
    if (fprintf(record, "%s\t@cwd=%s", tool, directory) < 0) failed = 1;
    for (index = 1; !failed && index < argc; index++) {
        if (fprintf(record, "\t%s", argv[index]) < 0) failed = 1;
    }
    if (!failed && fputc('\n', record) == EOF) failed = 1;
    if (fclose(record) != 0) failed = 1;
    free(directory);
    return failed ? -1 : 0;
}

static int run_iverilog(int argc, char **argv)
{
    int index;

    for (index = 1; index < argc; index++) {
        if (strcmp(argv[index], "-tnull") == 0) return 0;
    }
    for (index = 1; index + 1 < argc; index++) {
        if (strcmp(argv[index], "-o") == 0) {
            return write_file(argv[index + 1], "fake vvp image\n") == 0 ? 0 : 74;
        }
    }
    return 65;
}

static int run_vvp(void)
{
    const char *waveform = getenv("SOLAR_BACKEND_WAVEFORM");
    const char *failing_test = getenv("SOLAR_BACKEND_FAIL_TEST");
    char *directory = current_directory();
    const char *test_name;
    int status;

    if (directory == NULL) return 74;
    test_name = program_name(directory);
    if (failing_test != NULL && strcmp(test_name, failing_test) == 0) {
        free(directory);
        return 67;
    }
    if (waveform == NULL || waveform[0] == '\0') waveform = "simulation.vcd";
    if (getenv("SOLAR_BACKEND_LARGE_OUTPUT") != NULL) {
        char block[4096];
        size_t remaining = (size_t)17U * 1024U * 1024U;

        (void)memset(block, 'x', sizeof(block));
        while (remaining > 0U) {
            size_t count = remaining < sizeof(block) ? remaining : sizeof(block);

            if (fwrite(block, 1U, count, stdout) != count) {
                free(directory);
                return 74;
            }
            remaining -= count;
        }
    }
    (void)printf("display from %s\n", test_name);
    if (getenv("SOLAR_BACKEND_NO_WAVEFORM") != NULL) {
        free(directory);
        return 0;
    }
    status = write_file(waveform, "fake vcd\n") == 0 ? 0 : 74;
    free(directory);
    return status;
}

static int run_verilator(int argc, char **argv)
{
    int index;

    for (index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--lint-only") == 0) return 0;
    }
    for (index = 1; index + 1 < argc; index++) {
        if (strcmp(argv[index], "-o") == 0) {
            char *source = realpath(argv[0], NULL);
            FILE *input;
            FILE *output;
            unsigned char buffer[16384];
            size_t count;
            int failed = 0;

            if (source == NULL) return 74;
            input = fopen(source, "rb");
            free(source);
            if (input == NULL) return 74;
            output = fopen(argv[index + 1], "wb");
            if (output == NULL) {
                (void)fclose(input);
                return 74;
            }
            while ((count = fread(buffer, 1U, sizeof(buffer), input)) > 0U) {
                if (fwrite(buffer, 1U, count, output) != count) {
                    failed = 1;
                    break;
                }
            }
            if (ferror(input) != 0) failed = 1;
            if (fclose(input) != 0) failed = 1;
            if (fclose(output) != 0) failed = 1;
            if (failed || chmod(argv[index + 1], 0700) != 0) return 74;
            return 0;
        }
    }
    return 65;
}

static int run_python(int argc, char **argv)
{
    int index;

    for (index = 1; index + 1 < argc; index++) {
        if (strcmp(argv[index], "--waveform") == 0) {
            (void)printf("cocotb fake test passed\n");
            if (getenv("SOLAR_BACKEND_NO_WAVEFORM") != NULL) return 0;
            return write_file(argv[index + 1], "fake fst\n") == 0 ? 0 : 74;
        }
    }
    return 65;
}

int main(int argc, char **argv)
{
    const char *tool = program_name(argv[0]);

    if (record_invocation(tool, argc, argv) != 0) return 74;
    if (strcmp(tool, "iverilog") == 0) return run_iverilog(argc, argv);
    if (strcmp(tool, "vvp") == 0) return run_vvp();
    if (strcmp(tool, "verilator") == 0) return run_verilator(argc, argv);
    if (strcmp(tool, "simulation") == 0) return run_vvp();
    if (strcmp(tool, "python3") == 0) return run_python(argc, argv);
    return 64;
}

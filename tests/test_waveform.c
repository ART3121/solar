#define _POSIX_C_SOURCE 200809L

#include "solar/waveform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char *join_path(const char *left, const char *right)
{
    size_t length = strlen(left) + strlen(right) + 2U;
    char *path = malloc(length);

    if (path != NULL) (void)snprintf(path, length, "%s/%s", left, right);
    return path;
}

static int write_file(const char *path)
{
    FILE *file = fopen(path, "w");
    int failed;

    if (file == NULL) return -1;
    failed = fputs("fake fst\n", file) == EOF;
    if (fclose(file) != 0) failed = 1;
    return failed ? -1 : 0;
}

static char *read_file(const char *path)
{
    FILE *file = fopen(path, "r");
    char *contents;
    long length;

    if (file == NULL || fseek(file, 0L, SEEK_END) != 0) {
        if (file != NULL) (void)fclose(file);
        return NULL;
    }
    length = ftell(file);
    if (length < 0L || fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        return NULL;
    }
    contents = malloc((size_t)length + 1U);
    if (contents == NULL) {
        (void)fclose(file);
        return NULL;
    }
    if (fread(contents, 1U, (size_t)length, file) != (size_t)length ||
        fclose(file) != 0) {
        free(contents);
        return NULL;
    }
    contents[length] = '\0';
    return contents;
}

static int wait_for_record(const char *path)
{
    int attempts;

    for (attempts = 0; attempts < 100 && access(path, R_OK) != 0; attempts++) {
        const struct timespec delay = {0, 10000000L};

        (void)nanosleep(&delay, NULL);
    }
    return access(path, R_OK);
}

static int record_matches(
    const char *record,
    const char *viewer,
    const char *waveform
)
{
    char *contents = read_file(record);
    size_t expected_length = strlen(viewer) + strlen(waveform) + 3U;
    char *expected = malloc(expected_length);
    int matches = 0;

    if (contents != NULL && expected != NULL) {
        (void)snprintf(
            expected, expected_length, "%s\n%s\n", viewer, waveform
        );
        matches = strcmp(contents, expected) == 0;
    }
    free(expected);
    free(contents);
    return matches;
}

int main(int argc, char **argv)
{
    char root_template[] = "/tmp/solar viewer-XXXXXX";
    char *root = mkdtemp(root_template);
    char *gtkwave = NULL;
    char *surfer = NULL;
    char *waveform = NULL;
    char *record = NULL;
    char *old_path = NULL;
    bool launched = false;
    SolarResult result;
    int failed = 1;

    if (argc != 2 || root == NULL) goto cleanup;
    gtkwave = join_path(root, "gtkwave");
    surfer = join_path(root, "surfer");
    waveform = join_path(root, "wave output.fst");
    record = join_path(root, "viewer.record");
    old_path = getenv("PATH") == NULL ? NULL : strdup(getenv("PATH"));
    if (gtkwave == NULL || surfer == NULL || waveform == NULL || record == NULL ||
        symlink(argv[1], gtkwave) != 0 || symlink(argv[1], surfer) != 0 ||
        write_file(waveform) != 0 ||
        setenv("PATH", root, 1) != 0 || setenv("DISPLAY", ":test", 1) != 0 ||
        setenv("SOLAR_VIEWER_RECORD", record, 1) != 0) goto cleanup;
    result = solar_waveform_open(waveform, false, &launched);
    if (result.status != SOLAR_STATUS_OK || launched || access(record, F_OK) == 0) {
        goto cleanup;
    }
    result = solar_waveform_open(waveform, true, &launched);
    if (result.status != SOLAR_STATUS_OK || !launched) goto cleanup;
    if (wait_for_record(record) != 0 ||
        !record_matches(record, "gtkwave", waveform)) goto cleanup;
    if (unlink(record) != 0) goto cleanup;
    launched = false;
    result = solar_waveform_open_with_viewer(
        waveform, SOLAR_WAVEFORM_VIEWER_SURFER, true, &launched
    );
    if (result.status != SOLAR_STATUS_OK || !launched ||
        wait_for_record(record) != 0 ||
        !record_matches(record, "surfer", waveform)) goto cleanup;
    launched = true;
    result = solar_waveform_open_with_viewer(
        waveform, (SolarWaveformViewer)99, true, &launched
    );
    if (result.status != SOLAR_STATUS_INVALID_ARGUMENT || launched) goto cleanup;
    if (unlink(surfer) != 0 || unlink(record) != 0) goto cleanup;
    launched = true;
    result = solar_waveform_open_with_viewer(
        waveform, SOLAR_WAVEFORM_VIEWER_SURFER, true, &launched
    );
    if (result.status != SOLAR_STATUS_TOOL_MISSING || launched ||
        strstr(result.diagnostic.message, "Surfer") == NULL) goto cleanup;
    failed = 0;

cleanup:
    if (old_path != NULL) (void)setenv("PATH", old_path, 1);
    else (void)unsetenv("PATH");
    (void)unsetenv("SOLAR_VIEWER_RECORD");
    if (gtkwave != NULL) (void)unlink(gtkwave);
    if (surfer != NULL) (void)unlink(surfer);
    if (waveform != NULL) (void)unlink(waveform);
    if (record != NULL) (void)unlink(record);
    if (root != NULL) (void)rmdir(root);
    free(old_path);
    free(record);
    free(waveform);
    free(surfer);
    free(gtkwave);
    return failed;
}

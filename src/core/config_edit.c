#include "solar/config_edit.h"

#include "solar/config.h"
#include "solar/discovery.h"
#include "solar/filesystem.h"
#include "solar/project.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} TextBuffer;

#define CONFIG_EDIT_MANIFEST_LIMIT ((off_t)16 * 1024 * 1024)

typedef enum {
    MANIFEST_SECTION_OTHER = 0,
    MANIFEST_SECTION_PROJECT,
    MANIFEST_SECTION_SYNTHESIS
} ManifestSection;

typedef struct {
    const SolarConfigEditRequest *request;
    bool project_seen;
    bool synthesis_seen;
    bool name_written;
    bool top_written;
    bool default_test_written;
} RewriteState;

static SolarResult edit_error(
    SolarStatus status,
    const char *message,
    const char *hint
)
{
    return solar_result_error(status, message, hint);
}

static SolarResult buffer_reserve(TextBuffer *buffer, size_t extra)
{
    size_t needed;
    size_t capacity;
    char *grown;

    if (buffer->length == SIZE_MAX ||
        extra > SIZE_MAX - buffer->length - 1U) {
        return edit_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "solar.toml is too large to edit",
            "reduce the manifest size and try again"
        );
    }
    needed = buffer->length + extra + 1U;
    if (needed <= buffer->capacity) return solar_result_ok();
    capacity = buffer->capacity == 0U ? 4096U : buffer->capacity;
    while (capacity < needed) {
        capacity = capacity > SIZE_MAX / 2U ? needed : capacity * 2U;
    }
    grown = realloc(buffer->data, capacity);
    if (grown == NULL) {
        return edit_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate edited manifest text",
            "free memory and try again"
        );
    }
    buffer->data = grown;
    buffer->capacity = capacity;
    return solar_result_ok();
}

static SolarResult buffer_append(
    TextBuffer *buffer,
    const char *text,
    size_t length
)
{
    SolarResult result = buffer_reserve(buffer, length);

    if (result.status != SOLAR_STATUS_OK) return result;
    (void)memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return solar_result_ok();
}

static SolarResult buffer_text(TextBuffer *buffer, const char *text)
{
    return buffer_append(buffer, text, strlen(text));
}

static SolarResult buffer_quoted(TextBuffer *buffer, const char *value)
{
    const unsigned char *cursor = (const unsigned char *)value;
    SolarResult result = buffer_text(buffer, "\"");

    while (result.status == SOLAR_STATUS_OK && *cursor != '\0') {
        if (*cursor == '\\' || *cursor == '"') {
            char escaped[2] = {'\\', (char)*cursor};

            result = buffer_append(buffer, escaped, sizeof(escaped));
        } else {
            result = buffer_append(buffer, (const char *)cursor, 1U);
        }
        cursor++;
    }
    if (result.status == SOLAR_STATUS_OK) result = buffer_text(buffer, "\"");
    return result;
}

static SolarResult append_key(
    TextBuffer *buffer,
    const char *key,
    const char *value
)
{
    SolarResult result;

    if (buffer->length > 0U && buffer->data[buffer->length - 1U] != '\n') {
        result = buffer_text(buffer, "\n");
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    result = buffer_text(buffer, key);
    if (result.status == SOLAR_STATUS_OK) result = buffer_text(buffer, " = ");
    if (result.status == SOLAR_STATUS_OK) result = buffer_quoted(buffer, value);
    if (result.status == SOLAR_STATUS_OK) result = buffer_text(buffer, "\n");
    return result;
}

static const char *trim_line(const char *line, const char *end)
{
    while (line < end && (*line == ' ' || *line == '\t')) line++;
    return line;
}

static bool line_equals(const char *line, const char *end, const char *value)
{
    const char *start = trim_line(line, end);
    size_t length;

    while (end > start && (end[-1] == '\n' || end[-1] == '\r' ||
                           end[-1] == ' ' || end[-1] == '\t')) end--;
    length = (size_t)(end - start);
    return strlen(value) == length && strncmp(start, value, length) == 0;
}

static bool line_is_header(const char *line, const char *end)
{
    const char *start = trim_line(line, end);

    return start < end && *start == '[';
}

static bool line_is_key(const char *line, const char *end, const char *key)
{
    const char *start = trim_line(line, end);
    size_t length = strlen(key);

    if ((size_t)(end - start) < length || strncmp(start, key, length) != 0) {
        return false;
    }
    start += length;
    while (start < end && (*start == ' ' || *start == '\t')) start++;
    return start < end && *start == '=';
}

static ManifestSection section_for_header(const char *line, const char *end)
{
    if (line_equals(line, end, "[project]")) {
        return MANIFEST_SECTION_PROJECT;
    }
    if (line_equals(line, end, "[synthesis]")) {
        return MANIFEST_SECTION_SYNTHESIS;
    }
    return MANIFEST_SECTION_OTHER;
}

static SolarResult append_missing_keys(
    TextBuffer *output,
    ManifestSection section,
    RewriteState *state
)
{
    SolarResult result = solar_result_ok();

    if (section == MANIFEST_SECTION_PROJECT) {
        if (state->request->project_name != NULL && !state->name_written) {
            result = append_key(
                output, "name", state->request->project_name
            );
            state->name_written = result.status == SOLAR_STATUS_OK;
        }
        if (result.status == SOLAR_STATUS_OK &&
            state->request->default_test != NULL &&
            !state->default_test_written) {
            result = append_key(
                output, "default_test", state->request->default_test
            );
            state->default_test_written = result.status == SOLAR_STATUS_OK;
        }
    } else if (section == MANIFEST_SECTION_SYNTHESIS &&
               state->request->synthesis_top != NULL && !state->top_written) {
        result = append_key(
            output, "top", state->request->synthesis_top
        );
        state->top_written = result.status == SOLAR_STATUS_OK;
    }
    return result;
}

static SolarResult replace_managed_line(
    TextBuffer *output,
    ManifestSection section,
    const char *line,
    const char *end,
    RewriteState *state,
    bool *replaced_out
)
{
    const char *key = NULL;
    const char *value = NULL;
    bool *written = NULL;

    *replaced_out = false;
    if (section == MANIFEST_SECTION_PROJECT &&
        state->request->project_name != NULL &&
        line_is_key(line, end, "name")) {
        key = "name";
        value = state->request->project_name;
        written = &state->name_written;
    } else if (section == MANIFEST_SECTION_PROJECT &&
               state->request->default_test != NULL &&
               line_is_key(line, end, "default_test")) {
        key = "default_test";
        value = state->request->default_test;
        written = &state->default_test_written;
    } else if (section == MANIFEST_SECTION_SYNTHESIS &&
               state->request->synthesis_top != NULL &&
               line_is_key(line, end, "top")) {
        key = "top";
        value = state->request->synthesis_top;
        written = &state->top_written;
    }
    if (key != NULL) {
        SolarResult result = append_key(output, key, value);

        if (result.status == SOLAR_STATUS_OK) *written = true;
        *replaced_out = true;
        return result;
    }
    return solar_result_ok();
}

static SolarResult rewrite_manifest(
    const char *input,
    size_t input_length,
    const SolarConfigEditRequest *request,
    char **text_out,
    size_t *length_out
)
{
    const char *cursor = input;
    const char *limit = input + input_length;
    ManifestSection section = MANIFEST_SECTION_OTHER;
    RewriteState state = {request, false, false, false, false, false};
    TextBuffer output = {0};
    SolarResult result = solar_result_ok();

    *text_out = NULL;
    *length_out = 0U;
    while (cursor < limit && result.status == SOLAR_STATUS_OK) {
        const char *end = memchr(cursor, '\n', (size_t)(limit - cursor));
        bool replaced = false;

        end = end == NULL ? limit : end + 1;
        if (line_is_header(cursor, end)) {
            result = append_missing_keys(&output, section, &state);
            if (result.status != SOLAR_STATUS_OK) break;
            section = section_for_header(cursor, end);
            if (section == MANIFEST_SECTION_PROJECT) state.project_seen = true;
            if (section == MANIFEST_SECTION_SYNTHESIS) {
                state.synthesis_seen = true;
            }
        } else {
            result = replace_managed_line(
                &output, section, cursor, end, &state, &replaced
            );
        }
        if (result.status == SOLAR_STATUS_OK && !replaced) {
            result = buffer_append(&output, cursor, (size_t)(end - cursor));
        }
        cursor = end;
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = append_missing_keys(&output, section, &state);
    }
    if (result.status == SOLAR_STATUS_OK &&
        ((request->project_name != NULL || request->default_test != NULL) &&
         !state.project_seen)) {
        result = edit_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "solar.toml has no [project] section to update",
            "restore the required [project] section and run solar check"
        );
    }
    if (result.status == SOLAR_STATUS_OK && request->synthesis_top != NULL &&
        !state.synthesis_seen) {
        result = edit_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "solar.toml has no [synthesis] section to update",
            "restore the required [synthesis] section and run solar check"
        );
    }
    if (result.status != SOLAR_STATUS_OK) {
        free(output.data);
        return result;
    }
    *text_out = output.data;
    *length_out = output.length;
    return solar_result_ok();
}

static SolarResult read_manifest(
    const char *path,
    char **text_out,
    size_t *length_out
)
{
    int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat information;
    char *text;
    size_t length;
    size_t offset = 0U;

    *text_out = NULL;
    *length_out = 0U;
    if (descriptor < 0 || fstat(descriptor, &information) != 0 ||
        !S_ISREG(information.st_mode) || information.st_size < 0 ||
        information.st_size > CONFIG_EDIT_MANIFEST_LIMIT) {
        if (descriptor >= 0) (void)close(descriptor);
        return edit_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot safely read solar.toml for configuration editing",
            "use a regular manifest no larger than 16 MiB"
        );
    }
    length = (size_t)information.st_size;
    text = malloc(length + 1U);
    if (text == NULL) {
        (void)close(descriptor);
        return edit_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate solar.toml editing text",
            "free memory and try again"
        );
    }
    while (offset < length) {
        ssize_t count = read(descriptor, text + offset, length - offset);

        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            free(text);
            (void)close(descriptor);
            return edit_error(
                SOLAR_STATUS_IO_ERROR,
                "failed while reading solar.toml",
                "check the project filesystem and try again"
            );
        }
        offset += (size_t)count;
    }
    if (close(descriptor) != 0) {
        free(text);
        return edit_error(
            SOLAR_STATUS_IO_ERROR,
            "failed to close solar.toml after reading",
            "check the project filesystem and try again"
        );
    }
    text[length] = '\0';
    *text_out = text;
    *length_out = length;
    return solar_result_ok();
}

static SolarResult write_candidate(
    const char *root,
    const char *path,
    const char *text,
    size_t length,
    mode_t mode
)
{
    size_t written = 0U;
    int descriptor;
    SolarResult result = solar_filesystem_reset_artifact(
        root, ".solar/tmp/config/solar.toml.next"
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    descriptor = open(
        path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
        mode & 0777
    );
    if (descriptor < 0) {
        return edit_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot create candidate solar.toml",
            "check .solar/tmp permissions and try again"
        );
    }
    while (written < length) {
        ssize_t count = write(descriptor, text + written, length - written);

        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            result = edit_error(
                SOLAR_STATUS_IO_ERROR,
                "cannot write candidate solar.toml",
                "check free disk space and try again"
            );
            break;
        }
        written += (size_t)count;
    }
    if (result.status == SOLAR_STATUS_OK && fsync(descriptor) != 0) {
        result = edit_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot synchronize candidate solar.toml",
            "check the project filesystem and try again"
        );
    }
    if (close(descriptor) != 0 && result.status == SOLAR_STATUS_OK) {
        result = edit_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot close candidate solar.toml",
            "check the project filesystem and try again"
        );
    }
    if (result.status != SOLAR_STATUS_OK) (void)unlink(path);
    return result;
}

static SolarResult validate_candidate(const char *root, const char *path)
{
    SolarProject project;
    SolarConfig parsed;
    SolarResult result;

    solar_project_init(&project);
    solar_config_init(&parsed);
    result = solar_config_parse_file(path, &parsed);
    if (result.status != SOLAR_STATUS_OK) return result;
    project.root = strdup(root);
    project.manifest_path = strdup(path);
    if (project.root == NULL || project.manifest_path == NULL) {
        solar_config_free(&parsed);
        solar_project_free(&project);
        return edit_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate configuration validation state",
            "free memory and try again"
        );
    }
    project.config = parsed;
    solar_config_init(&parsed);
    result = solar_discovery_apply(&project, &project.discovery);
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_project_validate(&project);
    }
    solar_project_free(&project);
    return result;
}

static bool request_is_empty(const SolarConfigEditRequest *request)
{
    return request->project_name == NULL && request->synthesis_top == NULL &&
        request->default_test == NULL;
}

SolarResult solar_config_edit_project(
    const char *start_path,
    const SolarConfigEditRequest *request,
    bool *changed_out
)
{
    SolarProject project;
    struct stat information;
    char *input = NULL;
    char *candidate = NULL;
    char *candidate_path = NULL;
    char *root = NULL;
    size_t input_length = 0U;
    size_t candidate_length = 0U;
    SolarProjectLock project_lock;
    SolarResult result;

    if (changed_out != NULL) *changed_out = false;
    if (start_path == NULL || request == NULL || changed_out == NULL ||
        request_is_empty(request)) {
        return edit_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "configuration edit requires at least one value",
            "set --name, --top, or --test"
        );
    }
    solar_project_init(&project);
    solar_project_lock_init(&project_lock);
    result = solar_project_find_root(start_path, &root);
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_project_lock_acquire(root, &project_lock);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = solar_project_load(root, &project);
    }
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (project.config.manifest_format != 2U) {
        result = edit_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "solar config set requires project format 2",
            "run solar scan to migrate this manifest before editing configuration"
        );
        goto cleanup;
    }
    if (lstat(project.manifest_path, &information) != 0 ||
        !S_ISREG(information.st_mode) || S_ISLNK(information.st_mode)) {
        result = edit_error(
            SOLAR_STATUS_IO_ERROR,
            "solar.toml is not a safe regular file",
            "replace the manifest symlink or special file with a regular file"
        );
        goto cleanup;
    }
    result = read_manifest(project.manifest_path, &input, &input_length);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = rewrite_manifest(
        input, input_length, request, &candidate, &candidate_length
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (input_length == candidate_length &&
        memcmp(input, candidate, input_length) == 0) {
        result = solar_result_ok();
        goto cleanup;
    }
    result = solar_filesystem_prepare_generated_directory(
        project.root, ".solar/tmp/config"
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_join(
        project.root, ".solar/tmp/config/solar.toml.next", &candidate_path
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = write_candidate(
        project.root,
        candidate_path,
        candidate,
        candidate_length,
        information.st_mode
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = validate_candidate(project.root, candidate_path);
    if (result.status != SOLAR_STATUS_OK) {
        (void)unlink(candidate_path);
        goto cleanup;
    }
    if (rename(candidate_path, project.manifest_path) != 0) {
        (void)unlink(candidate_path);
        result = edit_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot atomically replace solar.toml",
            "check project filesystem permissions and try again"
        );
        goto cleanup;
    }
    {
        int descriptor = open(
            project.root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW
        );

        if (descriptor >= 0) {
            (void)fsync(descriptor);
            (void)close(descriptor);
        }
    }
    *changed_out = true;
    result = solar_result_ok();

cleanup:
    solar_project_lock_release(&project_lock);
    free(root);
    free(candidate_path);
    free(candidate);
    free(input);
    solar_project_free(&project);
    return result;
}

#define _GNU_SOURCE

#include "solar/artifact.h"

#include "solar/filesystem.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef RENAME_EXCHANGE
#define RENAME_EXCHANGE (1U << 1)
#endif

#define ARTIFACT_STATE_PATH ".solar/state/artifacts.tsv"
#define ARTIFACT_STATE_TEMP ".solar/state/artifacts.tsv.tmp"
#define ARTIFACT_STATE_SIZE_LIMIT ((off_t)16 * 1024 * 1024)

static SolarResult artifact_error(
    SolarStatus status,
    const char *hint,
    const char *format,
    ...
)
{
    SolarResult result;
    va_list arguments;

    result.status = status;
    result.diagnostic.severity = SOLAR_DIAGNOSTIC_ERROR;
    va_start(arguments, format);
    (void)vsnprintf(
        result.diagnostic.message,
        sizeof(result.diagnostic.message),
        format,
        arguments
    );
    va_end(arguments);
    (void)snprintf(
        result.diagnostic.hint,
        sizeof(result.diagnostic.hint),
        "%s",
        hint == NULL ? "" : hint
    );
    return result;
}

void solar_artifact_list_init(SolarArtifactList *list)
{
    if (list != NULL) (void)memset(list, 0, sizeof(*list));
}

static void record_free(SolarArtifactRecord *record)
{
    free(record->type);
    free(record->path);
    free(record->stage);
    free(record->test_name);
    free(record->backend);
    free(record->profile);
    (void)memset(record, 0, sizeof(*record));
}

void solar_artifact_list_free(SolarArtifactList *list)
{
    size_t index;

    if (list == NULL) return;
    for (index = 0U; index < list->count; index++) {
        record_free(&list->items[index]);
    }
    free(list->items);
    solar_artifact_list_init(list);
}

static bool clean_field(const char *value, bool allow_empty)
{
    const unsigned char *cursor;

    if (value == NULL) return allow_empty;
    if (!allow_empty && value[0] == '\0') return false;
    for (cursor = (const unsigned char *)value; *cursor != '\0'; cursor++) {
        if (*cursor < 32U || *cursor == 127U || *cursor == '\t') return false;
    }
    return true;
}

static bool path_below_root(const char *path, const char *root)
{
    size_t length = strlen(root);

    return strncmp(path, root, length) == 0 && path[length] != '\0';
}

static bool public_artifact_record(
    const char *path,
    const char *type,
    const char *stage,
    const char *backend
)
{
    static const char *const ROOTS[] = {
        "sim/", "synth/", "hardware/", "simulation/"
    };
    size_t index;

    if (!solar_filesystem_is_safe_relative(path)) return false;
    for (index = 0U; index < sizeof(ROOTS) / sizeof(ROOTS[0]); index++) {
        if (path_below_root(path, ROOTS[index])) return true;
    }
    return path_below_root(path, "software/") &&
        type != NULL && strcmp(type, "assembly") == 0 &&
        stage != NULL && strcmp(stage, "generation") == 0 &&
        backend != NULL && strcmp(backend, "yanc") == 0;
}

static bool internal_temporary_source(
    const char *project_root,
    const char *path
)
{
    size_t root_length;
    static const char SUFFIX[] = "/.solar/tmp/";

    if (project_root == NULL || path == NULL || project_root[0] != '/') {
        return false;
    }
    root_length = strlen(project_root);
    return strncmp(path, project_root, root_length) == 0 &&
        strncmp(path + root_length, SUFFIX, sizeof(SUFFIX) - 1U) == 0 &&
        path[root_length + sizeof(SUFFIX) - 1U] != '\0';
}

static SolarResult copy_field(char **output, const char *value)
{
    const char *source = value == NULL ? "" : value;

    *output = strdup(source);
    if (*output == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate artifact metadata",
            "free memory and try again"
        );
    }
    return solar_result_ok();
}

static SolarResult record_copy(
    SolarArtifactRecord *destination,
    const SolarArtifactRecord *source
)
{
    SolarResult result;

    (void)memset(destination, 0, sizeof(*destination));
    result = copy_field(&destination->type, source->type);
    if (result.status == SOLAR_STATUS_OK) {
        result = copy_field(&destination->path, source->path);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = copy_field(&destination->stage, source->stage);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = copy_field(&destination->test_name, source->test_name);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = copy_field(&destination->backend, source->backend);
    }
    if (result.status == SOLAR_STATUS_OK) {
        result = copy_field(&destination->profile, source->profile);
    }
    if (result.status != SOLAR_STATUS_OK) record_free(destination);
    return result;
}

static SolarResult append_record(
    SolarArtifactList *list,
    const SolarArtifactRecord *record
)
{
    SolarArtifactRecord *items = realloc(
        list->items, (list->count + 1U) * sizeof(*items)
    );
    SolarResult result;

    if (items == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not grow the artifact registry",
            "free memory and try again"
        );
    }
    list->items = items;
    result = record_copy(&list->items[list->count], record);
    if (result.status == SOLAR_STATUS_OK) list->count++;
    return result;
}

static SolarResult state_path(
    const char *project_root,
    const char *relative,
    char **path_out
)
{
    if (project_root == NULL || project_root[0] != '/') {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "artifact registry requires an absolute project root",
            "load the project before accessing generated artifacts"
        );
    }
    return solar_filesystem_join(project_root, relative, path_out);
}

static SolarResult parse_line(char *line, SolarArtifactRecord *record)
{
    char *fields[6];
    char *cursor = line;
    size_t index;

    (void)memset(record, 0, sizeof(*record));
    for (index = 0U; index < 6U; index++) {
        char *separator;

        fields[index] = cursor;
        separator = index == 5U ? NULL : strchr(cursor, '\t');
        if (index < 5U && separator == NULL) {
            return solar_result_error(
                SOLAR_STATUS_IO_ERROR,
                "artifact registry contains a malformed record",
                "remove .solar/state/artifacts.tsv and regenerate artifacts"
            );
        }
        if (separator != NULL) {
            *separator = '\0';
            cursor = separator + 1;
        }
    }
    cursor = strpbrk(fields[5], "\r\n");
    if (cursor != NULL) *cursor = '\0';
    if (!clean_field(fields[0], false) ||
        !clean_field(fields[2], false) ||
        !clean_field(fields[4], true) ||
        !public_artifact_record(
            fields[1], fields[0], fields[2],
            strcmp(fields[4], "-") == 0 ? "" : fields[4]
        )) {
        return solar_result_error(
            SOLAR_STATUS_IO_ERROR,
            "artifact registry contains unsafe metadata",
            "remove .solar/state/artifacts.tsv and regenerate artifacts"
        );
    }
    record->type = fields[0];
    record->path = fields[1];
    record->stage = fields[2];
    record->test_name = strcmp(fields[3], "-") == 0 ? "" : fields[3];
    record->backend = strcmp(fields[4], "-") == 0 ? "" : fields[4];
    record->profile = strcmp(fields[5], "-") == 0 ? "" : fields[5];
    return solar_result_ok();
}

SolarResult solar_artifact_list_load(
    const char *project_root,
    SolarArtifactList *list
)
{
    char *path = NULL;
    FILE *file = NULL;
    int descriptor = -1;
    struct stat information;
    char *line = NULL;
    size_t capacity = 0U;
    SolarResult result;

    if (list == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "artifact loading requires result storage",
            "provide an initialized artifact list"
        );
    }
    solar_artifact_list_init(list);
    result = state_path(project_root, ARTIFACT_STATE_PATH, &path);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        if (errno == ENOENT) result = solar_result_ok();
        else result = artifact_error(
            SOLAR_STATUS_IO_ERROR,
            "check permissions on .solar/state",
            "cannot open artifact registry %s: %s", path, strerror(errno)
        );
        goto cleanup;
    }
    if (fstat(descriptor, &information) != 0 ||
        !S_ISREG(information.st_mode) || information.st_size < 0 ||
        information.st_size > ARTIFACT_STATE_SIZE_LIMIT) {
        result = artifact_error(
            SOLAR_STATUS_IO_ERROR,
            "remove .solar/state/artifacts.tsv and regenerate artifacts",
            "artifact registry is not a regular file below 16 MiB"
        );
        goto cleanup;
    }
    file = fdopen(descriptor, "r");
    if (file == NULL) {
        result = artifact_error(
            SOLAR_STATUS_IO_ERROR,
            "check available file descriptors",
            "cannot read artifact registry: %s", strerror(errno)
        );
        goto cleanup;
    }
    descriptor = -1;
    while (getline(&line, &capacity, file) >= 0) {
        SolarArtifactRecord borrowed;

        if (line[0] == '\n' || line[0] == '\r' || line[0] == '\0') continue;
        result = parse_line(line, &borrowed);
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        result = append_record(list, &borrowed);
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
    }
    if (ferror(file) != 0) {
        result = artifact_error(
            SOLAR_STATUS_IO_ERROR,
            "check .solar/state storage",
            "cannot finish reading artifact registry: %s", strerror(errno)
        );
        goto cleanup;
    }
    result = solar_result_ok();

cleanup:
    free(line);
    if (file != NULL) (void)fclose(file);
    if (descriptor >= 0) (void)close(descriptor);
    free(path);
    if (result.status != SOLAR_STATUS_OK) solar_artifact_list_free(list);
    return result;
}

const SolarArtifactRecord *solar_artifact_find(
    const SolarArtifactList *list,
    const char *type,
    const char *test_name
)
{
    size_t index;

    if (list == NULL || type == NULL) return NULL;
    for (index = list->count; index > 0U; index--) {
        const SolarArtifactRecord *record = &list->items[index - 1U];
        bool test_matches = test_name == NULL ||
            strcmp(record->test_name, test_name) == 0;

        if (strcmp(record->type, type) == 0 && test_matches) return record;
    }
    return NULL;
}

static const char *stored_field(const char *value)
{
    return value == NULL || value[0] == '\0' ? "-" : value;
}

static SolarResult save_list(
    const char *project_root,
    const SolarArtifactList *list
)
{
    char *path = NULL;
    char *temporary = NULL;
    FILE *file = NULL;
    int descriptor = -1;
    size_t index;
    SolarResult result = solar_filesystem_prepare_generated_directory(
        project_root, ".solar/state"
    );

    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = state_path(project_root, ARTIFACT_STATE_PATH, &path);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = state_path(project_root, ARTIFACT_STATE_TEMP, &temporary);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    descriptor = open(
        temporary,
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
        0644
    );
    if (descriptor < 0 || (file = fdopen(descriptor, "w")) == NULL) {
        if (descriptor >= 0) (void)close(descriptor);
        descriptor = -1;
        result = artifact_error(
            SOLAR_STATUS_IO_ERROR,
            "check .solar/state permissions",
            "cannot create artifact registry: %s", strerror(errno)
        );
        goto cleanup;
    }
    descriptor = -1;
    for (index = 0U; index < list->count; index++) {
        const SolarArtifactRecord *record = &list->items[index];

        if (fprintf(
                file, "%s\t%s\t%s\t%s\t%s\t%s\n",
                record->type, record->path, record->stage,
                stored_field(record->test_name),
                stored_field(record->backend),
                stored_field(record->profile)
            ) < 0) {
            result = artifact_error(
                SOLAR_STATUS_IO_ERROR,
                "check .solar/state storage",
                "cannot write artifact registry: %s", strerror(errno)
            );
            goto cleanup;
        }
    }
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) {
        result = artifact_error(
            SOLAR_STATUS_IO_ERROR,
            "check .solar/state storage",
            "cannot finish artifact registry: %s", strerror(errno)
        );
        goto cleanup;
    }
    if (fclose(file) != 0) {
        file = NULL;
        result = artifact_error(
            SOLAR_STATUS_IO_ERROR,
            "check .solar/state storage",
            "cannot close artifact registry: %s", strerror(errno)
        );
        goto cleanup;
    }
    file = NULL;
    if (rename(temporary, path) != 0) {
        result = artifact_error(
            SOLAR_STATUS_IO_ERROR,
            "check .solar/state permissions",
            "cannot publish artifact registry: %s", strerror(errno)
        );
        goto cleanup;
    }
    result = solar_result_ok();

cleanup:
    if (file != NULL) (void)fclose(file);
    if (descriptor >= 0) (void)close(descriptor);
    if (result.status != SOLAR_STATUS_OK && temporary != NULL) {
        (void)unlink(temporary);
    }
    free(temporary);
    free(path);
    return result;
}

static bool path_registered(
    const SolarArtifactList *list,
    const char *path
)
{
    size_t index;

    for (index = 0U; index < list->count; index++) {
        if (strcmp(list->items[index].path, path) == 0) return true;
    }
    return false;
}

static SolarResult replace_record(
    SolarArtifactList *list,
    const SolarArtifactRecord *record
)
{
    size_t index;

    for (index = 0U; index < list->count; index++) {
        if (strcmp(list->items[index].path, record->path) == 0) {
            SolarArtifactRecord replacement;
            SolarResult result = record_copy(&replacement, record);

            if (result.status != SOLAR_STATUS_OK) return result;
            record_free(&list->items[index]);
            list->items[index] = replacement;
            return solar_result_ok();
        }
    }
    return append_record(list, record);
}

static SolarResult open_public_parent(
    const char *project_root,
    const char *relative_path,
    bool create,
    int *parent_out,
    char **name_out
)
{
    char *copy = NULL;
    char *separator;
    char *component;
    char *save = NULL;
    int root = -1;
    int parent = -1;
    SolarResult result = solar_result_ok();

    *parent_out = -1;
    *name_out = NULL;
    copy = strdup(relative_path);
    if (copy == NULL) return solar_result_error(
        SOLAR_STATUS_INTERNAL_ERROR,
        "could not allocate public artifact path",
        "free memory and try again"
    );
    separator = strrchr(copy, '/');
    if (separator == NULL || separator[1] == '\0') {
        result = solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "public artifact has no parent directory",
            "publish below a supported project artifact directory"
        );
        goto cleanup;
    }
    *separator = '\0';
    *name_out = strdup(separator + 1);
    if (*name_out == NULL) {
        result = solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate public artifact name",
            "free memory and try again"
        );
        goto cleanup;
    }
    root = open(project_root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (root < 0) {
        result = artifact_error(
            SOLAR_STATUS_IO_ERROR,
            "check project directory permissions",
            "cannot open project root: %s", strerror(errno)
        );
        goto cleanup;
    }
    parent = root;
    component = strtok_r(copy, "/", &save);
    while (component != NULL) {
        int child;
        struct stat information;

        if (fstatat(parent, component, &information, AT_SYMLINK_NOFOLLOW) != 0) {
            if (errno != ENOENT || !create || mkdirat(parent, component, 0755) != 0) {
                result = artifact_error(
                    SOLAR_STATUS_IO_ERROR,
                    "check public artifact directory permissions",
                    "cannot create public directory %s: %s",
                    component, strerror(errno)
                );
                goto cleanup;
            }
        } else if (!S_ISDIR(information.st_mode) ||
                   S_ISLNK(information.st_mode)) {
            result = artifact_error(
                SOLAR_STATUS_IO_ERROR,
                "replace the conflicting public path manually",
                "unsafe public artifact directory: %s", component
            );
            goto cleanup;
        }
        child = openat(
            parent, component,
            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
        );
        if (child < 0) {
            result = artifact_error(
                SOLAR_STATUS_IO_ERROR,
                "check public artifact directory permissions",
                "cannot open public directory %s: %s",
                component, strerror(errno)
            );
            goto cleanup;
        }
        if (parent != root) (void)close(parent);
        parent = child;
        component = strtok_r(NULL, "/", &save);
    }
    *parent_out = parent;
    parent = -1;

cleanup:
    if (parent >= 0 && parent != root) (void)close(parent);
    if (root >= 0 && root != *parent_out) (void)close(root);
    if (result.status != SOLAR_STATUS_OK) {
        free(*name_out);
        *name_out = NULL;
    }
    free(copy);
    return result;
}

static SolarResult copy_candidate(
    const char *source,
    int parent,
    const char *candidate
)
{
    unsigned char buffer[16384];
    struct stat information;
    int input = -1;
    int output = -1;
    SolarResult result = solar_result_ok();

    input = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (input < 0 || fstat(input, &information) != 0 ||
        !S_ISREG(information.st_mode)) {
        result = artifact_error(
            SOLAR_STATUS_IO_ERROR,
            "ensure the backend produced a readable regular file in .solar/tmp",
            "cannot read temporary artifact %s: %s", source, strerror(errno)
        );
        goto cleanup;
    }
    output = openat(
        parent, candidate,
        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
        0644
    );
    if (output < 0) {
        result = artifact_error(
            SOLAR_STATUS_IO_ERROR,
            "remove stale Solar publication candidates and try again",
            "cannot create public artifact candidate: %s", strerror(errno)
        );
        goto cleanup;
    }
    for (;;) {
        ssize_t count = read(input, buffer, sizeof(buffer));
        size_t offset = 0U;

        if (count < 0 && errno == EINTR) continue;
        if (count < 0) {
            result = artifact_error(
                SOLAR_STATUS_IO_ERROR,
                "check temporary artifact storage",
                "cannot read temporary artifact: %s", strerror(errno)
            );
            goto cleanup;
        }
        if (count == 0) break;
        while (offset < (size_t)count) {
            ssize_t written = write(
                output, buffer + offset, (size_t)count - offset
            );

            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) {
                result = artifact_error(
                    SOLAR_STATUS_IO_ERROR,
                    "check public artifact storage",
                    "cannot write public artifact candidate: %s",
                    strerror(errno)
                );
                goto cleanup;
            }
            offset += (size_t)written;
        }
    }
    if (fsync(output) != 0) {
        result = artifact_error(
            SOLAR_STATUS_IO_ERROR,
            "check public artifact storage",
            "cannot sync public artifact candidate: %s", strerror(errno)
        );
    }

cleanup:
    if (output >= 0) (void)close(output);
    if (input >= 0) (void)close(input);
    if (result.status != SOLAR_STATUS_OK) (void)unlinkat(parent, candidate, 0);
    return result;
}

static bool exchange_disabled(void)
{
    const char *value = getenv("SOLAR_TEST_DISABLE_RENAME_EXCHANGE");

    return value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
}

static int rename_exchange(int parent, const char *left, const char *right)
{
    if (exchange_disabled()) {
        errno = EOPNOTSUPP;
        return -1;
    }
    return renameat2(parent, left, parent, right, RENAME_EXCHANGE);
}

static SolarResult rollback_publication(
    int parent,
    const char *name,
    const char *old_name,
    bool used_exchange,
    bool had_previous
)
{
    if (!had_previous) {
        return unlinkat(parent, name, 0) == 0
            ? solar_result_ok()
            : artifact_error(
                SOLAR_STATUS_IO_ERROR,
                "restore the artifact manually from project history",
                "cannot roll back unregistered public artifact: %s",
                strerror(errno)
            );
    }
    if (used_exchange) {
        if (rename_exchange(parent, name, old_name) == 0) {
            (void)unlinkat(parent, old_name, 0);
            return solar_result_ok();
        }
    } else {
        (void)unlinkat(parent, name, 0);
        if (renameat(parent, old_name, parent, name) == 0) {
            return solar_result_ok();
        }
    }
    return artifact_error(
        SOLAR_STATUS_IO_ERROR,
        "restore the previous artifact from the retained backup",
        "cannot roll back public artifact publication: %s", strerror(errno)
    );
}

SolarResult solar_artifact_publish_file(
    const char *project_root,
    const char *temporary_source,
    const char *public_relative_path,
    const char *type,
    const char *stage,
    const char *test_name,
    const char *backend,
    const char *profile,
    char **destination_out
)
{
    SolarArtifactList list;
    SolarArtifactRecord record;
    char candidate[96];
    char backup[96];
    char *name = NULL;
    char *destination = NULL;
    int parent = -1;
    struct stat information;
    bool had_previous = false;
    bool used_exchange = false;
    bool candidate_exists = false;
    SolarResult result;

    if (destination_out != NULL) *destination_out = NULL;
    if (destination_out == NULL ||
        !internal_temporary_source(project_root, temporary_source) ||
        !public_artifact_record(
            public_relative_path, type, stage, backend
        ) ||
        !clean_field(type, false) || !clean_field(stage, false) ||
        !clean_field(test_name, true) || !clean_field(backend, true) ||
        !clean_field(profile, true)) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot publish an unsafe artifact record",
            "use a safe public path and structured artifact metadata"
        );
    }
    solar_artifact_list_init(&list);
    result = solar_artifact_list_load(project_root, &list);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = open_public_parent(
        project_root, public_relative_path, true, &parent, &name
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (fstatat(parent, name, &information, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!S_ISREG(information.st_mode) || S_ISLNK(information.st_mode) ||
            !path_registered(&list, public_relative_path)) {
            result = artifact_error(
                SOLAR_STATUS_IO_ERROR,
                "move the manual file or choose another generated artifact name",
                "refusing to overwrite unregistered public file %s",
                public_relative_path
            );
            goto cleanup;
        }
        had_previous = true;
    } else if (errno != ENOENT) {
        result = artifact_error(
            SOLAR_STATUS_IO_ERROR,
            "check public artifact permissions",
            "cannot inspect public artifact %s: %s",
            public_relative_path, strerror(errno)
        );
        goto cleanup;
    }
    (void)snprintf(
        candidate, sizeof(candidate), ".solar-candidate-%ld",
        (long)getpid()
    );
    (void)snprintf(
        backup, sizeof(backup), ".solar-backup-%ld",
        (long)getpid()
    );
    (void)unlinkat(parent, candidate, 0);
    (void)unlinkat(parent, backup, 0);
    result = copy_candidate(temporary_source, parent, candidate);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    candidate_exists = true;

    if (!had_previous) {
        if (renameat(parent, candidate, parent, name) != 0) {
            result = artifact_error(
                SOLAR_STATUS_IO_ERROR,
                "the temporary output remains available in .solar/tmp",
                "cannot publish artifact %s: %s",
                public_relative_path, strerror(errno)
            );
            goto cleanup;
        }
        candidate_exists = false;
    } else if (rename_exchange(parent, candidate, name) == 0) {
        used_exchange = true;
    } else if (errno == ENOSYS || errno == EINVAL ||
               errno == EOPNOTSUPP || errno == EXDEV) {
        if (renameat(parent, name, parent, backup) != 0) {
            result = artifact_error(
                SOLAR_STATUS_IO_ERROR,
                "the previous public artifact remains unchanged",
                "cannot stage previous artifact backup: %s", strerror(errno)
            );
            goto cleanup;
        }
        if (renameat(parent, candidate, parent, name) != 0) {
            int saved_errno = errno;

            (void)renameat(parent, backup, parent, name);
            result = artifact_error(
                SOLAR_STATUS_IO_ERROR,
                "the previous public artifact was restored",
                "cannot publish replacement artifact: %s",
                strerror(saved_errno)
            );
            goto cleanup;
        }
        candidate_exists = false;
    } else {
        result = artifact_error(
            SOLAR_STATUS_IO_ERROR,
            "the previous public artifact remains unchanged",
            "cannot exchange replacement artifact: %s", strerror(errno)
        );
        goto cleanup;
    }

    record.type = (char *)type;
    record.path = (char *)public_relative_path;
    record.stage = (char *)stage;
    record.test_name = (char *)(test_name == NULL ? "" : test_name);
    record.backend = (char *)(backend == NULL ? "" : backend);
    record.profile = (char *)(profile == NULL ? "" : profile);
    result = replace_record(&list, &record);
    if (result.status == SOLAR_STATUS_OK) result = save_list(project_root, &list);
    if (result.status != SOLAR_STATUS_OK) {
        SolarResult rollback = rollback_publication(
            parent, name, used_exchange ? candidate : backup,
            used_exchange, had_previous
        );

        if (rollback.status != SOLAR_STATUS_OK) result = rollback;
        goto cleanup;
    }
    if (used_exchange) (void)unlinkat(parent, candidate, 0);
    else if (had_previous) (void)unlinkat(parent, backup, 0);
    result = solar_filesystem_join(
        project_root, public_relative_path, &destination
    );
    if (result.status == SOLAR_STATUS_OK) {
        *destination_out = destination;
        destination = NULL;
    }

cleanup:
    if (candidate_exists && parent >= 0) (void)unlinkat(parent, candidate, 0);
    if (parent >= 0) (void)close(parent);
    free(destination);
    free(name);
    solar_artifact_list_free(&list);
    return result;
}

typedef struct {
    const SolarArtifactPublication *publication;
    char *name;
    char candidate[64];
    char backup[64];
    int parent;
    bool had_previous;
    bool used_exchange;
    bool applied;
} ArtifactTransactionEntry;

static void transaction_entry_cleanup(ArtifactTransactionEntry *entry)
{
    if (entry->parent >= 0) {
        (void)unlinkat(entry->parent, entry->candidate, 0);
        (void)unlinkat(entry->parent, entry->backup, 0);
        (void)close(entry->parent);
    }
    free(entry->name);
}

static SolarResult transaction_apply(ArtifactTransactionEntry *entry)
{
    if (!entry->had_previous) {
        if (renameat(
                entry->parent, entry->candidate,
                entry->parent, entry->name
            ) != 0) {
            return artifact_error(
                SOLAR_STATUS_IO_ERROR,
                "the previous public artifact set remains unchanged",
                "cannot publish artifact %s: %s",
                entry->publication->public_relative_path, strerror(errno)
            );
        }
    } else if (rename_exchange(
                   entry->parent, entry->candidate, entry->name
               ) == 0) {
        entry->used_exchange = true;
    } else if (errno == ENOSYS || errno == EINVAL ||
               errno == EOPNOTSUPP || errno == EXDEV) {
        if (renameat(
                entry->parent, entry->name,
                entry->parent, entry->backup
            ) != 0) {
            return artifact_error(
                SOLAR_STATUS_IO_ERROR,
                "the previous public artifact set remains unchanged",
                "cannot preserve artifact %s: %s",
                entry->publication->public_relative_path, strerror(errno)
            );
        }
        if (renameat(
                entry->parent, entry->candidate,
                entry->parent, entry->name
            ) != 0) {
            int saved_errno = errno;

            (void)renameat(
                entry->parent, entry->backup,
                entry->parent, entry->name
            );
            return artifact_error(
                SOLAR_STATUS_IO_ERROR,
                "the previous public artifact set was restored",
                "cannot publish artifact %s: %s",
                entry->publication->public_relative_path,
                strerror(saved_errno)
            );
        }
    } else {
        return artifact_error(
            SOLAR_STATUS_IO_ERROR,
            "the previous public artifact set remains unchanged",
            "cannot exchange artifact %s: %s",
            entry->publication->public_relative_path, strerror(errno)
        );
    }
    entry->applied = true;
    return solar_result_ok();
}

static SolarResult transaction_rollback(ArtifactTransactionEntry *entry)
{
    if (!entry->applied) return solar_result_ok();
    if (!entry->had_previous) {
        if (unlinkat(entry->parent, entry->name, 0) == 0) {
            entry->applied = false;
            return solar_result_ok();
        }
    } else if (entry->used_exchange) {
        if (rename_exchange(
                entry->parent, entry->name, entry->candidate
            ) == 0) {
            entry->applied = false;
            return solar_result_ok();
        }
    } else {
        (void)unlinkat(entry->parent, entry->name, 0);
        if (renameat(
                entry->parent, entry->backup,
                entry->parent, entry->name
            ) == 0) {
            entry->applied = false;
            return solar_result_ok();
        }
    }
    return artifact_error(
        SOLAR_STATUS_IO_ERROR,
        "restore the retained artifact backups manually",
        "cannot roll back artifact set: %s", strerror(errno)
    );
}

SolarResult solar_artifact_publish_files(
    const char *project_root,
    const SolarArtifactPublication *publications,
    size_t count
)
{
    SolarArtifactList list;
    ArtifactTransactionEntry *entries = NULL;
    size_t index;
    SolarResult result;

    solar_artifact_list_init(&list);
    if (project_root == NULL || publications == NULL || count == 0U) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "artifact set publication requires at least one file",
            "provide validated temporary and public artifact paths"
        );
    }
    entries = calloc(count, sizeof(*entries));
    if (entries == NULL) return solar_result_error(
        SOLAR_STATUS_INTERNAL_ERROR,
        "could not allocate artifact publication state",
        "free memory and try again"
    );
    for (index = 0U; index < count; index++) entries[index].parent = -1;
    result = solar_artifact_list_load(project_root, &list);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    for (index = 0U; index < count; index++) {
        const SolarArtifactPublication *item = &publications[index];
        SolarArtifactRecord record;
        struct stat information;
        size_t previous;

        if (!internal_temporary_source(project_root, item->temporary_source) ||
            !public_artifact_record(
                item->public_relative_path, item->type,
                item->stage, item->backend
            ) ||
            !clean_field(item->type, false) ||
            !clean_field(item->stage, false) ||
            !clean_field(item->test_name, true) ||
            !clean_field(item->backend, true) ||
            !clean_field(item->profile, true)) {
            result = solar_result_error(
                SOLAR_STATUS_INVALID_ARGUMENT,
                "cannot publish an unsafe artifact set",
                "use safe public paths and structured artifact metadata"
            );
            goto cleanup;
        }
        for (previous = 0U; previous < index; previous++) {
            if (strcmp(
                    publications[previous].public_relative_path,
                    item->public_relative_path
                ) == 0) {
                result = artifact_error(
                    SOLAR_STATUS_INVALID_ARGUMENT,
                    "publish each public artifact path only once",
                    "duplicate artifact destination %s",
                    item->public_relative_path
                );
                goto cleanup;
            }
        }
        entries[index].publication = item;
        result = open_public_parent(
            project_root, item->public_relative_path, true,
            &entries[index].parent, &entries[index].name
        );
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        if (fstatat(
                entries[index].parent, entries[index].name,
                &information, AT_SYMLINK_NOFOLLOW
            ) == 0) {
            if (!S_ISREG(information.st_mode) || S_ISLNK(information.st_mode) ||
                !path_registered(&list, item->public_relative_path)) {
                result = artifact_error(
                    SOLAR_STATUS_IO_ERROR,
                    "move the manual file or choose another generated artifact name",
                    "refusing to overwrite unregistered public file %s",
                    item->public_relative_path
                );
                goto cleanup;
            }
            entries[index].had_previous = true;
        } else if (errno != ENOENT) {
            result = artifact_error(
                SOLAR_STATUS_IO_ERROR,
                "check public artifact permissions",
                "cannot inspect artifact %s: %s",
                item->public_relative_path, strerror(errno)
            );
            goto cleanup;
        }
        (void)snprintf(
            entries[index].candidate, sizeof(entries[index].candidate),
            ".solar-candidate-%ld-%zu", (long)getpid(), index
        );
        (void)snprintf(
            entries[index].backup, sizeof(entries[index].backup),
            ".solar-backup-%ld-%zu", (long)getpid(), index
        );
        (void)unlinkat(
            entries[index].parent, entries[index].candidate, 0
        );
        (void)unlinkat(entries[index].parent, entries[index].backup, 0);
        result = copy_candidate(
            item->temporary_source, entries[index].parent,
            entries[index].candidate
        );
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        record.type = (char *)item->type;
        record.path = (char *)item->public_relative_path;
        record.stage = (char *)item->stage;
        record.test_name = (char *)(item->test_name == NULL ? "" : item->test_name);
        record.backend = (char *)(item->backend == NULL ? "" : item->backend);
        record.profile = (char *)(item->profile == NULL ? "" : item->profile);
        result = replace_record(&list, &record);
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
    }
    for (index = 0U; index < count; index++) {
        result = transaction_apply(&entries[index]);
        if (result.status != SOLAR_STATUS_OK) goto rollback;
    }
    result = save_list(project_root, &list);
    if (result.status != SOLAR_STATUS_OK) goto rollback;
    goto cleanup;

rollback:
    for (index = count; index > 0U; index--) {
        SolarResult rollback = transaction_rollback(&entries[index - 1U]);

        if (rollback.status != SOLAR_STATUS_OK) result = rollback;
    }

cleanup:
    if (entries != NULL) {
        for (index = 0U; index < count; index++) {
            transaction_entry_cleanup(&entries[index]);
        }
    }
    free(entries);
    solar_artifact_list_free(&list);
    return result;
}

static SolarResult remove_public_file(
    const char *project_root,
    const char *relative_path,
    bool *removed_out
)
{
    int parent = -1;
    char *name = NULL;
    char *absolute = NULL;
    struct stat information;
    SolarResult result;

    *removed_out = false;
    result = solar_filesystem_join(project_root, relative_path, &absolute);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (lstat(absolute, &information) != 0 && errno == ENOENT) {
        result = solar_result_ok();
        goto cleanup;
    }
    result = open_public_parent(
        project_root, relative_path, false, &parent, &name
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (fstatat(parent, name, &information, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) result = solar_result_ok();
        else result = artifact_error(
            SOLAR_STATUS_IO_ERROR,
            "check public artifact permissions",
            "cannot inspect registered artifact %s: %s",
            relative_path, strerror(errno)
        );
        goto cleanup;
    }
    if (!S_ISREG(information.st_mode) || S_ISLNK(information.st_mode)) {
        result = artifact_error(
            SOLAR_STATUS_IO_ERROR,
            "remove the conflicting path manually",
            "refusing to clean unsafe registered artifact %s", relative_path
        );
        goto cleanup;
    }
    if (unlinkat(parent, name, 0) != 0) {
        result = artifact_error(
            SOLAR_STATUS_IO_ERROR,
            "check public artifact permissions",
            "cannot remove registered artifact %s: %s",
            relative_path, strerror(errno)
        );
        goto cleanup;
    }
    *removed_out = true;
    result = solar_result_ok();

cleanup:
    if (parent >= 0) (void)close(parent);
    free(absolute);
    free(name);
    return result;
}

SolarResult solar_artifact_remove_registered(
    const char *project_root,
    size_t *removed_count_out
)
{
    SolarArtifactList list;
    size_t index;
    size_t removed_count = 0U;
    SolarResult result;

    if (removed_count_out != NULL) *removed_count_out = 0U;
    if (removed_count_out == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "artifact cleanup requires result storage",
            "provide a removed artifact count"
        );
    }
    solar_artifact_list_init(&list);
    result = solar_artifact_list_load(project_root, &list);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    for (index = 0U; index < list.count; index++) {
        bool removed = false;

        result = remove_public_file(
            project_root, list.items[index].path, &removed
        );
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        if (removed) removed_count++;
    }
    *removed_count_out = removed_count;

cleanup:
    solar_artifact_list_free(&list);
    return result;
}

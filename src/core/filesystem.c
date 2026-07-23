#define _GNU_SOURCE

#include "solar/filesystem.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static SolarResult filesystem_error(
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

SolarResult solar_filesystem_join(
    const char *left,
    const char *right,
    char **path_out
)
{
    size_t left_length;
    size_t right_offset = 0U;
    size_t right_length;
    bool needs_separator;
    char *path;

    if (path_out != NULL) {
        *path_out = NULL;
    }
    if (left == NULL || right == NULL || path_out == NULL || left[0] == '\0' ||
        right[0] == '\0' || right[0] == '/') {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot join an empty path",
            "provide a base directory and a relative path"
        );
    }

    left_length = strlen(left);
    right_length = strlen(right + right_offset);
    needs_separator = left[left_length - 1U] != '/';

    if (right_length > SIZE_MAX - 2U ||
        left_length > SIZE_MAX - right_length - 2U) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "joined path is too long to represent",
            "use shorter project-relative paths"
        );
    }

    path = malloc(left_length + (needs_separator ? 1U : 0U) + right_length + 1U);
    if (path == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate a path",
            "free memory and try again"
        );
    }

    (void)memcpy(path, left, left_length);
    if (needs_separator) {
        path[left_length] = '/';
        left_length++;
    }
    (void)memcpy(path + left_length, right + right_offset, right_length + 1U);
    *path_out = path;
    return solar_result_ok();
}

bool solar_filesystem_is_safe_relative(const char *path)
{
    const char *component;
    const char *separator;
    const unsigned char *character;
    size_t length;

    if (path == NULL || path[0] == '\0' || path[0] == '/' ||
        path[strlen(path) - 1U] == '/') {
        return false;
    }
    for (character = (const unsigned char *)path; *character != '\0'; character++) {
        if (*character < 32U || *character == 127U) {
            return false;
        }
    }

    component = path;
    while (*component != '\0') {
        separator = strchr(component, '/');
        length = separator == NULL ? strlen(component) : (size_t)(separator - component);
        if (length == 0U || (length == 1U && component[0] == '.') ||
            (length == 2U && component[0] == '.' && component[1] == '.')) {
            return false;
        }
        if (separator == NULL) {
            break;
        }
        component = separator + 1;
    }

    return true;
}

static bool is_generated_path(const char *path, bool allow_root)
{
    if (path == NULL) return false;
    if (allow_root && strcmp(path, ".solar") == 0) return true;
    return strncmp(path, ".solar/", 7U) == 0;
}

static SolarResult open_directory_at(
    int parent_descriptor,
    const char *name,
    const char *display_path,
    int *descriptor_out
)
{
    struct stat information;
    int descriptor;

    *descriptor_out = -1;
    if (fstatat(parent_descriptor, name, &information, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno != ENOENT) {
            return filesystem_error(
                SOLAR_STATUS_IO_ERROR,
                "check project directory permissions and try again",
                "cannot inspect %s: %s",
                display_path,
                strerror(errno)
            );
        }
        if (mkdirat(parent_descriptor, name, 0755) != 0 && errno != EEXIST) {
            return filesystem_error(
                SOLAR_STATUS_IO_ERROR,
                "check project directory permissions and try again",
                "cannot create %s: %s",
                display_path,
                strerror(errno)
            );
        }
        if (fstatat(parent_descriptor, name, &information, AT_SYMLINK_NOFOLLOW) != 0) {
            return filesystem_error(
                SOLAR_STATUS_IO_ERROR,
                "check the generated directory and try again",
                "cannot inspect newly created %s: %s",
                display_path,
                strerror(errno)
            );
        }
    }

    if (!S_ISDIR(information.st_mode) || S_ISLNK(information.st_mode)) {
        return filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "remove the conflicting path; generated directories cannot be symlinks",
            "unsafe generated directory: %s",
            display_path
        );
    }
    descriptor = openat(
        parent_descriptor,
        name,
        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
    );
    if (descriptor < 0) {
        return filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "check project directory permissions and try again",
            "cannot open %s: %s",
            display_path,
            strerror(errno)
        );
    }
    *descriptor_out = descriptor;
    return solar_result_ok();
}

void solar_project_lock_init(SolarProjectLock *lock)
{
    if (lock != NULL) lock->descriptor = -1;
}

void solar_project_lock_release(SolarProjectLock *lock)
{
    if (lock == NULL) return;
    if (lock->descriptor >= 0) (void)close(lock->descriptor);
    lock->descriptor = -1;
}

SolarResult solar_project_lock_acquire(
    const char *project_root,
    SolarProjectLock *lock
)
{
    int root_descriptor = -1;
    SolarResult result;

    if (project_root == NULL || project_root[0] != '/' || lock == NULL ||
        lock->descriptor >= 0) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot acquire an invalid Solar project lock",
            "load one project and initialize its operation lock"
        );
    }
    root_descriptor = open(
        project_root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
    );
    if (root_descriptor < 0) {
        return filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "check project directory permissions and unsafe symlinks",
            "cannot open Solar project %s for locking: %s",
            project_root, strerror(errno)
        );
    }
    if (flock(root_descriptor, LOCK_EX | LOCK_NB) != 0) {
        int saved_errno = errno;

        (void)close(root_descriptor);
        root_descriptor = -1;
        if (saved_errno == EACCES || saved_errno == EAGAIN) {
            result = filesystem_error(
                SOLAR_STATUS_IO_ERROR,
                "wait for the active Solar operation to finish and try again",
                "another Solar operation is already active in %s",
                project_root
            );
        } else {
            result = filesystem_error(
                SOLAR_STATUS_IO_ERROR,
                "use a Linux filesystem that supports advisory directory locks",
                "cannot lock Solar project %s: %s",
                project_root, strerror(saved_errno)
            );
        }
        return result;
    }
    lock->descriptor = root_descriptor;
    return solar_result_ok();
}

SolarResult solar_filesystem_prepare_layout(const char *project_root)
{
    if (project_root == NULL || project_root[0] != '/') {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot prepare generated data without an absolute project root",
            "load the project before preparing its build directories"
        );
    }
    return solar_filesystem_prepare_generated_directory(project_root, ".solar");
}

SolarResult solar_filesystem_prepare_generated_directory(
    const char *project_root,
    const char *relative_path
)
{
    char *path_copy = NULL;
    char *component;
    char *save_pointer = NULL;
    char *display_path = NULL;
    int root_descriptor = -1;
    int parent_descriptor = -1;
    SolarResult result = solar_result_ok();

    if (project_root == NULL || relative_path == NULL || project_root[0] != '/' ||
        !solar_filesystem_is_safe_relative(relative_path) ||
        !is_generated_path(relative_path, true)) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "generated directory must be below .solar",
            "use an owned generated path without '.' or '..' components"
        );
    }
    path_copy = strdup(relative_path);
    if (path_copy == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate generated directory path",
            "free memory and try again"
        );
    }
    root_descriptor = open(
        project_root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
    );
    if (root_descriptor < 0) {
        result = filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "check project directory permissions and try again",
            "cannot open project root %s: %s", project_root, strerror(errno)
        );
        goto cleanup;
    }
    parent_descriptor = root_descriptor;
    component = strtok_r(path_copy, "/", &save_pointer);
    while (component != NULL) {
        int child_descriptor = -1;
        char *next_display = NULL;

        result = solar_filesystem_join(
            display_path == NULL ? project_root : display_path,
            component,
            &next_display
        );
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        free(display_path);
        display_path = next_display;
        result = open_directory_at(
            parent_descriptor, component, display_path, &child_descriptor
        );
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        if (parent_descriptor != root_descriptor) (void)close(parent_descriptor);
        parent_descriptor = child_descriptor;
        component = strtok_r(NULL, "/", &save_pointer);
    }

cleanup:
    if (parent_descriptor >= 0 && parent_descriptor != root_descriptor) {
        (void)close(parent_descriptor);
    }
    if (root_descriptor >= 0) (void)close(root_descriptor);
    free(display_path);
    free(path_copy);
    return result;
}

static SolarResult open_existing_child_directory(
    int parent_descriptor,
    const char *name,
    const char *relative_path,
    int *descriptor_out
)
{
    int descriptor = openat(
        parent_descriptor,
        name,
        O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
    );

    if (descriptor < 0) {
        return filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "regenerate the .solar layout and remove conflicting symlinks",
            "cannot safely open generated path component %s in %s: %s",
            name,
            relative_path,
            strerror(errno)
        );
    }
    *descriptor_out = descriptor;
    return solar_result_ok();
}

SolarResult solar_filesystem_reset_artifact(
    const char *project_root,
    const char *relative_path
)
{
    char *parent_copy = NULL;
    char *file_name;
    char *separator;
    char *component;
    char *save_pointer = NULL;
    int root_descriptor = -1;
    int parent_descriptor = -1;
    struct stat information;
    SolarResult result = solar_result_ok();

    if (project_root == NULL || relative_path == NULL || project_root[0] != '/' ||
        !solar_filesystem_is_safe_relative(relative_path) ||
        !is_generated_path(relative_path, false)) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "artifact reset requires a safe generated path",
            "use a project-relative .solar path"
        );
    }
    parent_copy = strdup(relative_path);
    if (parent_copy == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate artifact path",
            "free memory and try again"
        );
    }
    separator = strrchr(parent_copy, '/');
    if (separator == NULL || separator[1] == '\0') {
        result = solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "artifact path has no file name",
            "provide a generated file below .solar"
        );
        goto cleanup;
    }
    *separator = '\0';
    file_name = separator + 1;

    root_descriptor = open(
        project_root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
    );
    if (root_descriptor < 0) {
        result = filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "check project directory permissions and try again",
            "cannot open project root %s: %s",
            project_root,
            strerror(errno)
        );
        goto cleanup;
    }
    parent_descriptor = root_descriptor;
    component = strtok_r(parent_copy, "/", &save_pointer);
    while (component != NULL) {
        int child_descriptor = -1;

        result = open_existing_child_directory(
            parent_descriptor, component, relative_path, &child_descriptor
        );
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        if (parent_descriptor != root_descriptor) {
            (void)close(parent_descriptor);
        }
        parent_descriptor = child_descriptor;
        component = strtok_r(NULL, "/", &save_pointer);
    }

    if (fstatat(parent_descriptor, file_name, &information, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno != ENOENT) {
            result = filesystem_error(
                SOLAR_STATUS_IO_ERROR,
                "check generated-data permissions and try again",
                "cannot inspect artifact %s: %s",
                relative_path,
                strerror(errno)
            );
        }
        goto cleanup;
    }
    if (!S_ISREG(information.st_mode) || S_ISLNK(information.st_mode)) {
        result = filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "remove the conflicting path manually; Solar will not follow it",
            "refusing to replace unsafe artifact %s",
            relative_path
        );
        goto cleanup;
    }
    if (unlinkat(parent_descriptor, file_name, 0) != 0) {
        result = filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "check generated-data permissions and try again",
            "cannot replace artifact %s: %s",
            relative_path,
            strerror(errno)
        );
    }

cleanup:
    if (parent_descriptor >= 0 && parent_descriptor != root_descriptor) {
        (void)close(parent_descriptor);
    }
    if (root_descriptor >= 0) (void)close(root_descriptor);
    free(parent_copy);
    return result;
}

SolarResult solar_filesystem_create_generated_symlink(
    const char *project_root,
    const char *relative_path,
    const char *absolute_target
)
{
    char *parent_copy = NULL;
    char *name;
    char *separator;
    char *component;
    char *save_pointer = NULL;
    const unsigned char *character;
    int root_descriptor = -1;
    int parent_descriptor = -1;
    struct stat information;
    SolarResult result = solar_result_ok();

    if (project_root == NULL || relative_path == NULL ||
        absolute_target == NULL || project_root[0] != '/' ||
        absolute_target[0] != '/' ||
        !solar_filesystem_is_safe_relative(relative_path) ||
        !is_generated_path(relative_path, false)) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "generated symlink requires a safe generated path and absolute target",
            "use a generated link name and a validated absolute directory"
        );
    }
    for (character = (const unsigned char *)absolute_target;
         *character != '\0';
         character++) {
        if (*character < 32U || *character == 127U) {
            return solar_result_error(
                SOLAR_STATUS_INVALID_ARGUMENT,
                "generated symlink target contains control characters",
                "use a normal absolute include directory"
            );
        }
    }
    parent_copy = strdup(relative_path);
    if (parent_copy == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate generated symlink path",
            "free memory and try again"
        );
    }
    separator = strrchr(parent_copy, '/');
    if (separator == NULL || separator[1] == '\0') {
        result = solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "generated symlink path has no link name",
            "provide a link below the project .solar directory"
        );
        goto cleanup;
    }
    *separator = '\0';
    name = separator + 1;
    root_descriptor = open(
        project_root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
    );
    if (root_descriptor < 0) {
        result = filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "check project directory permissions and try again",
            "cannot open project root %s: %s", project_root, strerror(errno)
        );
        goto cleanup;
    }
    parent_descriptor = root_descriptor;
    component = strtok_r(parent_copy, "/", &save_pointer);
    while (component != NULL) {
        int child_descriptor = -1;

        result = open_existing_child_directory(
            parent_descriptor, component, relative_path, &child_descriptor
        );
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
        if (parent_descriptor != root_descriptor) (void)close(parent_descriptor);
        parent_descriptor = child_descriptor;
        component = strtok_r(NULL, "/", &save_pointer);
    }
    if (fstatat(parent_descriptor, name, &information, AT_SYMLINK_NOFOLLOW) == 0) {
        result = filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "remove the conflicting generated path and try again",
            "refusing to replace existing generated link %s",
            relative_path
        );
        goto cleanup;
    }
    if (errno != ENOENT) {
        result = filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "check generated-data permissions and try again",
            "cannot inspect generated link %s: %s",
            relative_path, strerror(errno)
        );
        goto cleanup;
    }
    if (symlinkat(absolute_target, parent_descriptor, name) != 0) {
        result = filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "check generated-data permissions and try again",
            "cannot create generated link %s: %s",
            relative_path, strerror(errno)
        );
    }

cleanup:
    if (parent_descriptor >= 0 && parent_descriptor != root_descriptor) {
        (void)close(parent_descriptor);
    }
    if (root_descriptor >= 0) (void)close(root_descriptor);
    free(parent_copy);
    return result;
}

static SolarResult remove_entry_at(
    int parent_descriptor,
    const char *name,
    const char *display_path
)
{
    struct stat information;

    if (fstatat(parent_descriptor, name, &information, AT_SYMLINK_NOFOLLOW) != 0) {
        return filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "check generated-data permissions and try again",
            "cannot inspect %s while cleaning: %s",
            display_path,
            strerror(errno)
        );
    }
    if (S_ISDIR(information.st_mode) && !S_ISLNK(information.st_mode)) {
        int descriptor = openat(
            parent_descriptor,
            name,
            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
        );
        DIR *directory;
        struct dirent *entry;

        if (descriptor < 0) {
            return filesystem_error(
                SOLAR_STATUS_IO_ERROR,
                "check generated-data permissions and try again",
                "cannot open %s while cleaning: %s",
                display_path,
                strerror(errno)
            );
        }
        directory = fdopendir(descriptor);
        if (directory == NULL) {
            int saved_errno = errno;
            (void)close(descriptor);
            return filesystem_error(
                SOLAR_STATUS_IO_ERROR,
                "check generated-data permissions and try again",
                "cannot read %s while cleaning: %s",
                display_path,
                strerror(saved_errno)
            );
        }

        errno = 0;
        while ((entry = readdir(directory)) != NULL) {
            char *child_path = NULL;
            SolarResult result;

            if (strcmp(entry->d_name, ".") == 0 ||
                strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            result = solar_filesystem_join(
                display_path, entry->d_name, &child_path
            );
            if (result.status == SOLAR_STATUS_OK) {
                result = remove_entry_at(descriptor, entry->d_name, child_path);
            }
            free(child_path);
            if (result.status != SOLAR_STATUS_OK) {
                (void)closedir(directory);
                return result;
            }
            errno = 0;
        }
        if (errno != 0) {
            int saved_errno = errno;
            (void)closedir(directory);
            return filesystem_error(
                SOLAR_STATUS_IO_ERROR,
                "check generated-data permissions and try again",
                "cannot finish reading %s: %s",
                display_path,
                strerror(saved_errno)
            );
        }
        if (closedir(directory) != 0) {
            return filesystem_error(
                SOLAR_STATUS_IO_ERROR,
                "check generated-data permissions and try again",
                "cannot close %s while cleaning: %s",
                display_path,
                strerror(errno)
            );
        }
        if (unlinkat(parent_descriptor, name, AT_REMOVEDIR) != 0) {
            return filesystem_error(
                SOLAR_STATUS_IO_ERROR,
                "check generated-data permissions and try again",
                "cannot remove directory %s: %s",
                display_path,
                strerror(errno)
            );
        }
        return solar_result_ok();
    }

    if (unlinkat(parent_descriptor, name, 0) != 0) {
        return filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "check generated-data permissions and try again",
            "cannot remove generated entry %s: %s",
            display_path,
            strerror(errno)
        );
    }
    return solar_result_ok();
}

SolarResult solar_filesystem_remove_generated_tree(
    const char *project_root,
    const char *relative_path,
    bool *removed_out
)
{
    char *parent_copy = NULL;
    char *display_path = NULL;
    char *name;
    char *separator;
    char *component;
    char *save_pointer = NULL;
    int root_descriptor = -1;
    int parent_descriptor = -1;
    struct stat information;
    SolarResult result = solar_result_ok();

    if (removed_out != NULL) *removed_out = false;
    if (project_root == NULL || relative_path == NULL || removed_out == NULL ||
        project_root[0] != '/' ||
        !solar_filesystem_is_safe_relative(relative_path) ||
        !is_generated_path(relative_path, false)) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "generated removal requires a safe owned path",
            "use a .solar path without '.' or '..' components"
        );
    }
    parent_copy = strdup(relative_path);
    if (parent_copy == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate generated removal path",
            "free memory and try again"
        );
    }
    separator = strrchr(parent_copy, '/');
    if (separator == NULL || separator[1] == '\0') {
        result = solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "generated removal path has no target name",
            "provide a path below the project .solar directory"
        );
        goto cleanup;
    }
    *separator = '\0';
    name = separator + 1;
    root_descriptor = open(
        project_root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
    );
    if (root_descriptor < 0) {
        result = filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "check project directory permissions and try again",
            "cannot open project root %s: %s", project_root, strerror(errno)
        );
        goto cleanup;
    }
    parent_descriptor = root_descriptor;
    component = strtok_r(parent_copy, "/", &save_pointer);
    while (component != NULL) {
        int child_descriptor = -1;

        result = open_existing_child_directory(
            parent_descriptor, component, relative_path, &child_descriptor
        );
        if (result.status != SOLAR_STATUS_OK) {
            if (errno == ENOENT) result = solar_result_ok();
            goto cleanup;
        }
        if (parent_descriptor != root_descriptor) (void)close(parent_descriptor);
        parent_descriptor = child_descriptor;
        component = strtok_r(NULL, "/", &save_pointer);
    }
    if (fstatat(parent_descriptor, name, &information, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno != ENOENT) {
            result = filesystem_error(
                SOLAR_STATUS_IO_ERROR,
                "check generated-data permissions and try again",
                "cannot inspect generated path %s: %s",
                relative_path, strerror(errno)
            );
        }
        goto cleanup;
    }
    result = solar_filesystem_join(project_root, relative_path, &display_path);
    if (result.status == SOLAR_STATUS_OK) {
        result = remove_entry_at(parent_descriptor, name, display_path);
    }
    if (result.status == SOLAR_STATUS_OK) *removed_out = true;

cleanup:
    if (parent_descriptor >= 0 && parent_descriptor != root_descriptor) {
        (void)close(parent_descriptor);
    }
    if (root_descriptor >= 0) (void)close(root_descriptor);
    free(display_path);
    free(parent_copy);
    return result;
}

static SolarResult inspect_publish_directory(
    const char *path,
    bool may_be_absent,
    bool *exists_out
)
{
    struct stat information;

    *exists_out = false;
    if (lstat(path, &information) != 0) {
        if (may_be_absent && errno == ENOENT) return solar_result_ok();
        return filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "check generated build paths and permissions",
            "cannot inspect publish directory %s: %s", path, strerror(errno)
        );
    }
    if (!S_ISDIR(information.st_mode) || S_ISLNK(information.st_mode)) {
        return filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "remove the conflicting path manually; Solar will not follow it",
            "refusing to publish through unsafe path %s", path
        );
    }
    *exists_out = true;
    return solar_result_ok();
}

SolarResult solar_filesystem_publish_generated_directory(
    const char *project_root,
    const char *staging_relative_path,
    const char *final_relative_path
)
{
    char *staging_path = NULL;
    char *final_path = NULL;
    char *backup_path = NULL;
    bool staging_exists = false;
    bool final_exists = false;
    bool removed = false;
    SolarResult result;

    if (project_root == NULL || staging_relative_path == NULL ||
        final_relative_path == NULL || strcmp(staging_relative_path,
        final_relative_path) == 0 ||
        !solar_filesystem_is_safe_relative(staging_relative_path) ||
        !solar_filesystem_is_safe_relative(final_relative_path) ||
        !is_generated_path(staging_relative_path, false) ||
        !is_generated_path(final_relative_path, false)) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "publish paths must be distinct owned generated directories",
            "use generated staging and final paths without '.' or '..' components"
        );
    }
    result = solar_filesystem_join(
        project_root, staging_relative_path, &staging_path
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = solar_filesystem_join(project_root, final_relative_path, &final_path);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = inspect_publish_directory(staging_path, false, &staging_exists);
    if (result.status != SOLAR_STATUS_OK || !staging_exists) goto cleanup;
    result = inspect_publish_directory(final_path, true, &final_exists);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;

    if (final_exists) {
        /*
         * Linux RENAME_EXCHANGE keeps final_path continuously visible. After
         * success, staging_path contains the previous valid build and can be
         * removed without affecting the newly published result.
         */
        if (renameat2(
                AT_FDCWD, staging_path, AT_FDCWD, final_path,
                RENAME_EXCHANGE
            ) != 0) {
            int exchange_errno = errno;

            if (exchange_errno != ENOSYS && exchange_errno != EINVAL &&
                exchange_errno != EOPNOTSUPP && exchange_errno != EXDEV) {
                result = filesystem_error(
                    SOLAR_STATUS_IO_ERROR,
                    "the previous generated build remains unchanged",
                    "cannot atomically publish generated build %s: %s",
                    final_path, strerror(exchange_errno)
                );
                goto cleanup;
            }
            if (asprintf(
                    &backup_path, "%s.previous-%ld", staging_path,
                    (long)getpid()
                ) < 0) {
                backup_path = NULL;
                result = solar_result_error(
                    SOLAR_STATUS_INTERNAL_ERROR,
                    "could not allocate publication backup path",
                    "free memory and try again"
                );
                goto cleanup;
            }
            if (rename(final_path, backup_path) != 0) {
                result = filesystem_error(
                    SOLAR_STATUS_IO_ERROR,
                    "the previous generated build remains unchanged",
                    "cannot preserve previous generated build: %s",
                    strerror(errno)
                );
                goto cleanup;
            }
            if (rename(staging_path, final_path) != 0) {
                int saved_errno = errno;

                (void)rename(backup_path, final_path);
                result = filesystem_error(
                    SOLAR_STATUS_IO_ERROR,
                    "the previous generated build was restored",
                    "cannot publish generated build %s: %s",
                    final_path, strerror(saved_errno)
                );
                goto cleanup;
            }
            if (rename(backup_path, staging_path) != 0) {
                /*
                 * Publication is already complete. A retained internal backup
                 * is preferable to reporting the valid new build as failed.
                 * A later clean removes it with the rest of .solar/.
                 */
                result = solar_result_ok();
                goto cleanup;
            }
        }
        (void)solar_filesystem_remove_generated_tree(
            project_root, staging_relative_path, &removed
        );
        result = solar_result_ok();
    } else {
        if (rename(staging_path, final_path) != 0) {
            result = filesystem_error(
                SOLAR_STATUS_IO_ERROR,
                "no previous generated build was changed",
                "cannot publish generated build %s: %s",
                final_path, strerror(errno)
            );
            goto cleanup;
        }
        result = solar_result_ok();
    }

cleanup:
    free(backup_path);
    free(final_path);
    free(staging_path);
    return result;
}

static bool target_is_protected(const char *project_root, const char *target)
{
    struct passwd *account;
    char *home = NULL;
    bool protected_target;

    if (strcmp(target, "/") == 0 || strcmp(target, project_root) == 0) {
        return true;
    }
    account = getpwuid(getuid());
    if (account != NULL && account->pw_dir != NULL) {
        home = realpath(account->pw_dir, NULL);
    }
    protected_target = home != NULL && strcmp(target, home) == 0;
    free(home);
    return protected_target;
}

SolarResult solar_filesystem_clean_project(
    const char *project_root,
    bool *removed_out
)
{
    char *canonical_root = NULL;
    char *target = NULL;
    int root_descriptor = -1;
    struct stat information;
    SolarResult result = solar_result_ok();

    if (removed_out != NULL) {
        *removed_out = false;
    }
    if (project_root == NULL || removed_out == NULL) {
        return solar_result_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot clean without a project root and result storage",
            "load a Solar project before cleaning"
        );
    }
    canonical_root = realpath(project_root, NULL);
    if (canonical_root == NULL) {
        return filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "load an existing Solar project before cleaning",
            "cannot resolve project root %s: %s",
            project_root,
            strerror(errno)
        );
    }
    root_descriptor = open(
        canonical_root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC
    );
    if (root_descriptor < 0) {
        result = filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "check project directory permissions and try again",
            "cannot open project root %s: %s",
            canonical_root,
            strerror(errno)
        );
        goto cleanup;
    }
    result = solar_filesystem_join(canonical_root, ".solar", &target);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (target_is_protected(canonical_root, target)) {
        result = filesystem_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "clean only Solar-owned internal data",
            "refusing to remove protected path %s", target
        );
        goto cleanup;
    }
    if (fstatat(root_descriptor, ".solar", &information,
                AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) goto cleanup;
        result = filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "check project directory permissions and try again",
            "cannot inspect %s: %s", target, strerror(errno)
        );
        goto cleanup;
    }
    if (!S_ISDIR(information.st_mode) || S_ISLNK(information.st_mode)) {
        result = filesystem_error(
            SOLAR_STATUS_IO_ERROR,
            "remove the conflicting path manually; Solar will not follow it",
            "refusing to clean unsafe generated path %s", target
        );
        goto cleanup;
    }
    result = remove_entry_at(root_descriptor, ".solar", target);
    if (result.status == SOLAR_STATUS_OK) *removed_out = true;

cleanup:
    if (root_descriptor >= 0) (void)close(root_descriptor);
    free(target);
    free(canonical_root);
    return result;
}

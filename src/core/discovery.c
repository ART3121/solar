#include "solar/discovery.h"

#include "solar/filesystem.h"
#include "solar/project.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DISCOVERY_SOURCE_SIZE_LIMIT ((off_t)64 * 1024 * 1024)

static SolarResult discovery_error(
    SolarStatus status,
    const char *message,
    const char *hint
)
{
    return solar_result_error(status, message, hint);
}

void solar_discovery_report_init(SolarDiscoveryReport *report)
{
    if (report != NULL) {
        (void)memset(report, 0, sizeof(*report));
    }
}

static bool list_contains(const SolarStringList *list, const char *value)
{
    size_t index;

    for (index = 0U; index < list->count; index++) {
        if (strcmp(list->items[index], value) == 0) return true;
    }
    return false;
}

static bool has_verilog_suffix(const char *name)
{
    size_t length = strlen(name);

    return (length > 2U && strcmp(name + length - 2U, ".v") == 0) ||
        (length > 3U && strcmp(name + length - 3U, ".sv") == 0);
}

static SolarResult join_relative(
    const char *parent,
    const char *name,
    char **path_out
)
{
    size_t parent_length = strlen(parent);
    size_t name_length = strlen(name);
    size_t length = parent_length + name_length + 2U;
    char *path = malloc(length);

    *path_out = NULL;
    if (path == NULL) {
        return discovery_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate a discovered source path",
            "free memory and try again"
        );
    }
    (void)snprintf(path, length, "%s/%s", parent, name);
    *path_out = path;
    return solar_result_ok();
}

static SolarResult collect_verilog_files(
    const char *project_root,
    const char *relative_directory,
    SolarStringList *files
)
{
    char *absolute_directory = NULL;
    DIR *directory = NULL;
    struct dirent *entry;
    SolarResult result = solar_filesystem_join(
        project_root, relative_directory, &absolute_directory
    );

    if (result.status != SOLAR_STATUS_OK) return result;
    directory = opendir(absolute_directory);
    if (directory == NULL && errno == ENOENT) {
        free(absolute_directory);
        return solar_result_ok();
    }
    if (directory == NULL) {
        free(absolute_directory);
        return discovery_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot read a conventional Solar source directory",
            "check permissions for rtl/ and tb/"
        );
    }

    while ((entry = readdir(directory)) != NULL) {
        char *relative_path = NULL;
        char *absolute_path = NULL;
        struct stat information;

        if (entry->d_name[0] == '.') continue;
        result = join_relative(
            relative_directory, entry->d_name, &relative_path
        );
        if (result.status != SOLAR_STATUS_OK) break;
        result = solar_filesystem_join(
            project_root, relative_path, &absolute_path
        );
        if (result.status != SOLAR_STATUS_OK) {
            free(relative_path);
            break;
        }
        if (lstat(absolute_path, &information) != 0) {
            free(absolute_path);
            free(relative_path);
            result = discovery_error(
                SOLAR_STATUS_IO_ERROR,
                "cannot inspect a discovered source path",
                "check project directory permissions"
            );
            break;
        }
        if (S_ISDIR(information.st_mode) && !S_ISLNK(information.st_mode)) {
            result = collect_verilog_files(
                project_root, relative_path, files
            );
        } else if (S_ISREG(information.st_mode) &&
                   has_verilog_suffix(entry->d_name)) {
            if (information.st_size < 0 ||
                information.st_size > DISCOVERY_SOURCE_SIZE_LIMIT) {
                result = discovery_error(
                    SOLAR_STATUS_CONFIG_ERROR,
                    "a discovered Verilog source exceeds the 64 MiB indexing limit",
                    "use smaller source files or explicit manifest paths"
                );
            } else {
                result = solar_string_list_append_copy(files, relative_path);
            }
        }
        free(absolute_path);
        free(relative_path);
        if (result.status != SOLAR_STATUS_OK) break;
    }

    if (closedir(directory) != 0 && result.status == SOLAR_STATUS_OK) {
        result = discovery_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot finish reading a Solar source directory",
            "check the project filesystem and try again"
        );
    }
    free(absolute_directory);
    return result;
}

static int compare_strings(const void *left, const void *right)
{
    const char *const *left_string = left;
    const char *const *right_string = right;

    return strcmp(*left_string, *right_string);
}

static SolarResult read_text_file(const char *path, char **text_out)
{
    int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat information;
    char *text;
    size_t length;
    size_t offset = 0U;

    *text_out = NULL;
    if (descriptor < 0 || fstat(descriptor, &information) != 0 ||
        !S_ISREG(information.st_mode)) {
        if (descriptor >= 0) (void)close(descriptor);
        return discovery_error(
            SOLAR_STATUS_IO_ERROR,
            "cannot read a discovered Verilog source",
            "check that files below rtl/ and tb/ are readable"
        );
    }
    if (information.st_size < 0 ||
        information.st_size > DISCOVERY_SOURCE_SIZE_LIMIT) {
        (void)close(descriptor);
        return discovery_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "a discovered Verilog source exceeds the 64 MiB indexing limit",
            "use smaller source files or explicit manifest paths"
        );
    }
    length = (size_t)information.st_size;
    text = malloc(length + 1U);
    if (text == NULL) {
        (void)close(descriptor);
        return discovery_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate Verilog discovery text",
            "free memory and try again"
        );
    }
    while (offset < length) {
        ssize_t count = read(descriptor, text + offset, length - offset);

        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            free(text);
            (void)close(descriptor);
            return discovery_error(
                SOLAR_STATUS_IO_ERROR,
                "failed while reading a discovered Verilog source",
                "check the project filesystem and try again"
            );
        }
        offset += (size_t)count;
    }
    if (close(descriptor) != 0) {
        free(text);
        return discovery_error(
            SOLAR_STATUS_IO_ERROR,
            "failed to close a discovered Verilog source",
            "check the project filesystem and try again"
        );
    }
    text[length] = '\0';
    *text_out = text;
    return solar_result_ok();
}

static bool identifier_start(unsigned char character)
{
    return isalpha(character) != 0 || character == '_';
}

static bool identifier_character(unsigned char character)
{
    return isalnum(character) != 0 || character == '_' || character == '$';
}

static bool next_verilog_token(
    const char **cursor_pointer,
    char *token,
    size_t capacity
)
{
    const char *cursor = *cursor_pointer;

    for (;;) {
        while (*cursor != '\0' && isspace((unsigned char)*cursor) != 0) cursor++;
        if (cursor[0] == '/' && cursor[1] == '/') {
            cursor += 2;
            while (*cursor != '\0' && *cursor != '\n') cursor++;
            continue;
        }
        if (cursor[0] == '/' && cursor[1] == '*') {
            const char *end = strstr(cursor + 2, "*/");
            cursor = end == NULL ? cursor + strlen(cursor) : end + 2;
            continue;
        }
        if (*cursor == '"') {
            cursor++;
            while (*cursor != '\0' && *cursor != '"') {
                if (*cursor == '\\' && cursor[1] != '\0') cursor++;
                cursor++;
            }
            if (*cursor == '"') cursor++;
            continue;
        }
        break;
    }
    if (*cursor == '\0') {
        *cursor_pointer = cursor;
        return false;
    }
    if (identifier_start((unsigned char)*cursor)) {
        size_t length = 0U;

        while (identifier_character((unsigned char)*cursor)) {
            if (length + 1U < capacity) token[length++] = *cursor;
            cursor++;
        }
        token[length] = '\0';
    } else {
        token[0] = *cursor++;
        token[1] = '\0';
    }
    *cursor_pointer = cursor;
    return true;
}

typedef struct {
    char *name;
    char *source_path;
    bool instantiated;
} DiscoveredModule;

typedef struct {
    DiscoveredModule *items;
    size_t count;
} ModuleIndex;

static void module_index_free(ModuleIndex *index)
{
    size_t item;

    if (index == NULL) return;
    for (item = 0U; item < index->count; item++) {
        free(index->items[item].name);
        free(index->items[item].source_path);
    }
    free(index->items);
    (void)memset(index, 0, sizeof(*index));
}

static DiscoveredModule *find_module(ModuleIndex *index, const char *name)
{
    size_t item;

    for (item = 0U; item < index->count; item++) {
        if (strcmp(index->items[item].name, name) == 0) {
            return &index->items[item];
        }
    }
    return NULL;
}

static SolarResult append_module(
    ModuleIndex *index,
    const char *name,
    const char *source_path
)
{
    DiscoveredModule *grown;
    DiscoveredModule module = {0};

    if (find_module(index, name) != NULL) {
        return discovery_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "a Verilog module is declared more than once",
            "give every module below rtl/ or tb/ a unique name"
        );
    }
    module.name = strdup(name);
    module.source_path = strdup(source_path);
    if (module.name == NULL || module.source_path == NULL) {
        free(module.name);
        free(module.source_path);
        return discovery_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate the discovered module index",
            "free memory and try again"
        );
    }
    grown = realloc(index->items, (index->count + 1U) * sizeof(*grown));
    if (grown == NULL) {
        free(module.name);
        free(module.source_path);
        return discovery_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not grow the discovered module index",
            "free memory and try again"
        );
    }
    index->items = grown;
    index->items[index->count++] = module;
    return solar_result_ok();
}

static bool read_module_name(const char **cursor, char *name, size_t capacity)
{
    if (!next_verilog_token(cursor, name, capacity)) return false;
    if (strcmp(name, "automatic") == 0 || strcmp(name, "static") == 0) {
        if (!next_verilog_token(cursor, name, capacity)) return false;
    }
    return identifier_start((unsigned char)name[0]);
}

static SolarResult collect_file_modules(
    const char *text,
    const char *source_path,
    ModuleIndex *index
)
{
    const char *cursor = text;
    char token[256];
    SolarResult result = solar_result_ok();

    while (next_verilog_token(&cursor, token, sizeof(token))) {
        if (strcmp(token, "module") != 0) continue;
        if (!read_module_name(&cursor, token, sizeof(token))) continue;
        result = append_module(index, token, source_path);
        if (result.status != SOLAR_STATUS_OK) break;
    }
    return result;
}

static bool skip_parenthesized(const char **cursor)
{
    char token[256];
    size_t depth = 1U;

    while (next_verilog_token(cursor, token, sizeof(token))) {
        if (strcmp(token, "(") == 0) depth++;
        else if (strcmp(token, ")") == 0 && --depth == 0U) return true;
    }
    return false;
}

static bool looks_like_instantiation(const char *cursor)
{
    char token[256];

    if (!next_verilog_token(&cursor, token, sizeof(token))) return false;
    if (strcmp(token, "#") == 0) {
        if (!next_verilog_token(&cursor, token, sizeof(token)) ||
            strcmp(token, "(") != 0 || !skip_parenthesized(&cursor)) {
            return false;
        }
        if (!next_verilog_token(&cursor, token, sizeof(token))) return false;
    }
    if (!identifier_start((unsigned char)token[0])) return false;
    return next_verilog_token(&cursor, token, sizeof(token)) &&
        strcmp(token, "(") == 0;
}

static void mark_file_instantiations(const char *text, ModuleIndex *index)
{
    const char *cursor = text;
    DiscoveredModule *parent = NULL;
    char token[256];

    while (next_verilog_token(&cursor, token, sizeof(token))) {
        DiscoveredModule *child;

        if (strcmp(token, "module") == 0) {
            parent = read_module_name(&cursor, token, sizeof(token))
                ? find_module(index, token) : NULL;
            continue;
        }
        if (strcmp(token, "endmodule") == 0) {
            parent = NULL;
            continue;
        }
        child = parent == NULL ? NULL : find_module(index, token);
        if (child != NULL && child != parent && looks_like_instantiation(cursor)) {
            child->instantiated = true;
        }
    }
}

static SolarResult build_module_index(
    const char *project_root,
    const SolarStringList *files,
    ModuleIndex *index
)
{
    size_t file_index;
    SolarResult result = solar_result_ok();

    for (file_index = 0U; file_index < files->count; file_index++) {
        char *absolute_path = NULL;
        char *text = NULL;

        result = solar_filesystem_join(
            project_root, files->items[file_index], &absolute_path
        );
        if (result.status == SOLAR_STATUS_OK) {
            result = read_text_file(absolute_path, &text);
        }
        if (result.status == SOLAR_STATUS_OK) {
            result = collect_file_modules(text, files->items[file_index], index);
        }
        free(text);
        free(absolute_path);
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    for (file_index = 0U; file_index < files->count; file_index++) {
        char *absolute_path = NULL;
        char *text = NULL;

        result = solar_filesystem_join(
            project_root, files->items[file_index], &absolute_path
        );
        if (result.status == SOLAR_STATUS_OK) {
            result = read_text_file(absolute_path, &text);
        }
        if (result.status == SOLAR_STATUS_OK) {
            mark_file_instantiations(text, index);
        }
        free(text);
        free(absolute_path);
        if (result.status != SOLAR_STATUS_OK) return result;
    }
    return result;
}

static char *test_name_from_path(const char *relative_path)
{
    const char *base = strrchr(relative_path, '/');
    size_t length;
    char *name;

    base = base == NULL ? relative_path : base + 1;
    length = strlen(base);
    if (length > 3U && strcmp(base + length - 3U, ".sv") == 0) {
        length -= 3U;
    } else if (length > 2U && strcmp(base + length - 2U, ".v") == 0) {
        length -= 2U;
    } else {
        return NULL;
    }
    name = malloc(length + 1U);
    if (name == NULL) return NULL;
    (void)memcpy(name, base, length);
    name[length] = '\0';
    return name;
}

static const char *select_test_top(
    const ModuleIndex *modules,
    const char *source_path,
    const char *file_stem,
    bool *ambiguous
)
{
    size_t index;
    const char *selected = NULL;
    size_t roots = 0U;

    *ambiguous = false;
    for (index = 0U; index < modules->count; index++) {
        const DiscoveredModule *module = &modules->items[index];

        if (module->instantiated ||
            strcmp(module->source_path, source_path) != 0) continue;
        selected = module->name;
        roots++;
        if (strcmp(module->name, file_stem) == 0) return module->name;
    }
    if (roots > 1U) {
        *ambiguous = true;
        return NULL;
    }
    return selected;
}

static const char *find_dumpfile_call(const char *text)
{
    const char *cursor = text;

    while (*cursor != '\0') {
        if (cursor[0] == '/' && cursor[1] == '/') {
            cursor += 2;
            while (*cursor != '\0' && *cursor != '\n') cursor++;
        } else if (cursor[0] == '/' && cursor[1] == '*') {
            const char *end = strstr(cursor + 2, "*/");
            cursor = end == NULL ? cursor + strlen(cursor) : end + 2;
        } else if (*cursor == '"') {
            cursor++;
            while (*cursor != '\0' && *cursor != '"') {
                if (*cursor == '\\' && cursor[1] != '\0') cursor++;
                cursor++;
            }
            if (*cursor == '"') cursor++;
        } else if (strncmp(cursor, "$dumpfile", 9U) == 0 &&
                   !identifier_character((unsigned char)cursor[9])) {
            return cursor + 9;
        } else {
            cursor++;
        }
    }
    return NULL;
}

static char *extract_dumpfile(const char *text, const char *fallback_name)
{
    const char *match = find_dumpfile_call(text);
    char *waveform;

    if (match != NULL) {
        const char *cursor = match;
        const char *end;
        size_t length;

        while (isspace((unsigned char)*cursor) != 0) cursor++;
        if (*cursor == '(') cursor++;
        while (isspace((unsigned char)*cursor) != 0) cursor++;
        if (*cursor == '"') {
            cursor++;
            end = strchr(cursor, '"');
            if (end != NULL) {
                length = (size_t)(end - cursor);
                waveform = malloc(length + 1U);
                if (waveform == NULL) return NULL;
                (void)memcpy(waveform, cursor, length);
                waveform[length] = '\0';
                return waveform;
            }
        }
    }
    waveform = malloc(strlen(fallback_name) + 5U);
    if (waveform != NULL) {
        (void)sprintf(waveform, "%s.vcd", fallback_name);
    }
    return waveform;
}

static bool test_references_source(
    const SolarConfig *config,
    const char *relative_path
)
{
    size_t index;

    for (index = 0U; index < config->test_count; index++) {
        if (list_contains(&config->tests[index].sources, relative_path)) {
            return true;
        }
    }
    return false;
}

static void free_local_test(SolarTest *test)
{
    free(test->name);
    free(test->top);
    free(test->waveform);
    solar_string_list_free(&test->sources);
    solar_string_list_free(&test->include_dirs);
    solar_string_list_free(&test->defines);
    (void)memset(test, 0, sizeof(*test));
}

static SolarResult append_discovered_test(
    SolarConfig *config,
    const char *relative_path,
    const char *top,
    const char *text
)
{
    SolarTest test = {0};
    SolarTest *tests;
    SolarResult result;

    test.name = test_name_from_path(relative_path);
    test.top = strdup(top);
    test.waveform = test.name == NULL ? NULL : extract_dumpfile(text, test.name);
    test.kind = SOLAR_TEST_VERILOG;
    if (test.name == NULL || test.top == NULL || test.waveform == NULL) {
        free_local_test(&test);
        return discovery_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not allocate a discovered test",
            "free memory and try again"
        );
    }
    if (solar_config_find_test(config, test.name) != NULL) {
        free_local_test(&test);
        return discovery_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "discovered testbench names are not unique",
            "use unique .v filenames below tb/ or declare tests explicitly"
        );
    }
    result = solar_string_list_append_copy(&test.sources, relative_path);
    if (result.status != SOLAR_STATUS_OK) {
        free_local_test(&test);
        return result;
    }
    tests = realloc(config->tests, (config->test_count + 1U) * sizeof(*tests));
    if (tests == NULL) {
        free_local_test(&test);
        return discovery_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not grow the discovered test list",
            "free memory and try again"
        );
    }
    config->tests = tests;
    config->tests[config->test_count++] = test;
    return solar_result_ok();
}

static SolarResult discover_test(
    SolarProject *project,
    const char *relative_path,
    const ModuleIndex *modules,
    SolarDiscoveryReport *report
)
{
    char *absolute_path = NULL;
    char *text = NULL;
    char *stem = NULL;
    const char *top;
    bool ambiguous = false;
    SolarResult result;

    if (test_references_source(&project->config, relative_path)) {
        return solar_result_ok();
    }
    result = solar_filesystem_join(project->root, relative_path, &absolute_path);
    if (result.status != SOLAR_STATUS_OK) return result;
    result = read_text_file(absolute_path, &text);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    stem = test_name_from_path(relative_path);
    if (stem == NULL) {
        result = discovery_error(
            SOLAR_STATUS_INTERNAL_ERROR,
            "could not derive a discovered test name",
            "use a non-empty .v filename below tb/"
        );
        goto cleanup;
    }
    top = select_test_top(modules, relative_path, stem, &ambiguous);
    if (top == NULL) {
        if (ambiguous) report->ambiguous_testbench_files++;
        goto cleanup;
    }
    result = append_discovered_test(
        &project->config, relative_path, top, text
    );
    if (result.status == SOLAR_STATUS_OK) report->tests_added++;

cleanup:
    free(stem);
    free(text);
    free(absolute_path);
    return result;
}

static SolarResult merge_rtl(
    SolarConfig *config,
    const SolarStringList *discovered,
    SolarDiscoveryReport *report
)
{
    size_t index;

    for (index = 0U; index < discovered->count; index++) {
        SolarResult result;

        if (list_contains(&config->sources.rtl, discovered->items[index])) {
            continue;
        }
        result = solar_string_list_append_copy(
            &config->sources.rtl, discovered->items[index]
        );
        if (result.status != SOLAR_STATUS_OK) return result;
        report->rtl_files_added++;
    }
    return solar_result_ok();
}

static void describe_root_candidates(
    const ModuleIndex *modules,
    char *hint,
    size_t capacity
)
{
    size_t used;
    size_t index;

    (void)snprintf(hint, capacity, "candidates:");
    used = strlen(hint);
    for (index = 0U; index < modules->count && used + 2U < capacity; index++) {
        int written;

        if (modules->items[index].instantiated) continue;
        written = snprintf(
            hint + used, capacity - used, " %s,", modules->items[index].name
        );
        if (written < 0 || (size_t)written >= capacity - used) break;
        used += (size_t)written;
    }
    if (used > 0U && hint[used - 1U] == ',') hint[used - 1U] = '\0';
}

static SolarResult select_synthesis_top(
    const ModuleIndex *modules,
    const char *project_name,
    const char **selected
)
{
    size_t roots = 0U;
    size_t index;

    *selected = NULL;
    if (modules->count == 0U) {
        return discovery_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "cannot infer a synthesis top because no RTL module was found",
            "declare at least one module below rtl/ or set an explicit top"
        );
    }
    for (index = 0U; index < modules->count; index++) {
        if (!modules->items[index].instantiated) {
            *selected = modules->items[index].name;
            roots++;
        }
    }
    if (roots == 0U) {
        return discovery_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "cannot infer a synthesis top from a cyclic RTL hierarchy",
            "break the module cycle or set an explicit top without running scan"
        );
    }
    if (roots == 1U) return solar_result_ok();
    *selected = NULL;
    if (project_name != NULL) {
        for (index = 0U; index < modules->count; index++) {
            if (!modules->items[index].instantiated &&
                strcmp(modules->items[index].name, project_name) == 0) {
                *selected = modules->items[index].name;
                return solar_result_ok();
            }
        }
    }
    {
        char hint[SOLAR_DIAGNOSTIC_HINT_SIZE];

        describe_root_candidates(modules, hint, sizeof(hint));
        return discovery_error(
            SOLAR_STATUS_CONFIG_ERROR,
            "cannot infer a unique synthesis top from the RTL hierarchy",
            hint
        );
    }
}

static SolarResult infer_synthesis_top(
    SolarProject *project,
    const SolarStringList *rtl_files,
    SolarDiscoveryReport *report
)
{
    ModuleIndex modules = {0};
    const char *selected = NULL;
    SolarResult result;

    if (project->config.synthesis.top != NULL) return solar_result_ok();
    result = build_module_index(project->root, rtl_files, &modules);
    if (result.status == SOLAR_STATUS_OK) {
        result = select_synthesis_top(
            &modules, project->config.project.name, &selected
        );
    }
    if (result.status == SOLAR_STATUS_OK) {
        project->config.synthesis.top = strdup(selected);
        if (project->config.synthesis.top == NULL) {
            result = discovery_error(
                SOLAR_STATUS_INTERNAL_ERROR,
                "could not allocate the inferred synthesis top",
                "free memory and try again"
            );
        } else {
            report->synthesis_top_inferred = true;
        }
    }
    module_index_free(&modules);
    return result;
}

static SolarResult merge_test_sources(
    SolarConfig *config,
    const SolarStringList *discovered,
    SolarDiscoveryReport *report
)
{
    size_t test_index;
    size_t source_index;

    for (test_index = 0U; test_index < config->test_count; test_index++) {
        SolarTest *test = &config->tests[test_index];

        if (test->kind != SOLAR_TEST_VERILOG) continue;
        for (source_index = 0U; source_index < discovered->count; source_index++) {
            SolarResult result;

            /* A discovered top-level testbench belongs to its derived test.
             * Files without an identifiable top are treated as shared helpers. */
            if (test_references_source(config, discovered->items[source_index])) {
                continue;
            }
            if (list_contains(&test->sources, discovered->items[source_index])) {
                continue;
            }
            result = solar_string_list_append_copy(
                &test->sources, discovered->items[source_index]
            );
            if (result.status != SOLAR_STATUS_OK) return result;
            report->test_sources_added++;
        }
    }
    return solar_result_ok();
}

SolarResult solar_discovery_apply(
    SolarProject *project,
    SolarDiscoveryReport *report
)
{
    SolarStringList rtl_files = {0};
    SolarStringList testbench_files = {0};
    SolarStringList automatic_testbench_files = {0};
    ModuleIndex testbench_modules = {0};
    SolarResult result = solar_result_ok();
    size_t index;

    if (project == NULL || report == NULL || project->root == NULL) {
        return discovery_error(
            SOLAR_STATUS_INVALID_ARGUMENT,
            "cannot discover sources for an unloaded project",
            "load a project before source discovery"
        );
    }
    solar_discovery_report_init(report);
    if (project->config.manifest_format != 2U ||
        project->config.project.language == NULL ||
        strcmp(project->config.project.language, "verilog") != 0) {
        return solar_result_ok();
    }
    result = collect_verilog_files(project->root, "rtl", &rtl_files);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = collect_verilog_files(project->root, "tb", &testbench_files);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    if (rtl_files.count > 1U) {
        qsort(
            rtl_files.items,
            rtl_files.count,
            sizeof(*rtl_files.items),
            compare_strings
        );
    }
    if (testbench_files.count > 1U) {
        qsort(
            testbench_files.items,
            testbench_files.count,
            sizeof(*testbench_files.items),
            compare_strings
        );
    }
    report->rtl_files_found = rtl_files.count;
    report->testbench_files_found = testbench_files.count;
    result = merge_rtl(&project->config, &rtl_files, report);
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    result = infer_synthesis_top(
        project, &project->config.sources.rtl, report
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    for (index = 0U; index < testbench_files.count; index++) {
        if (test_references_source(
                &project->config, testbench_files.items[index])) continue;
        result = solar_string_list_append_copy(
            &automatic_testbench_files, testbench_files.items[index]
        );
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
    }
    result = build_module_index(
        project->root, &automatic_testbench_files, &testbench_modules
    );
    if (result.status != SOLAR_STATUS_OK) goto cleanup;
    for (index = 0U; index < automatic_testbench_files.count; index++) {
        result = discover_test(
            project, automatic_testbench_files.items[index],
            &testbench_modules, report
        );
        if (result.status != SOLAR_STATUS_OK) goto cleanup;
    }
    result = merge_test_sources(&project->config, &testbench_files, report);

cleanup:
    module_index_free(&testbench_modules);
    solar_string_list_free(&automatic_testbench_files);
    solar_string_list_free(&testbench_files);
    solar_string_list_free(&rtl_files);
    return result;
}

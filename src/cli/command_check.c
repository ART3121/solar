#include "commands.h"

#include "solar/project.h"

#include <stdio.h>
#include <string.h>

static const SolarTest *default_test(const SolarConfig *config)
{
    if (config->project.default_test != NULL) {
        return solar_config_find_test(config, config->project.default_test);
    }
    return config->test_count == 1U ? &config->tests[0] : NULL;
}

static void print_configured_tests(const SolarConfig *config)
{
    const SolarTest *selected = default_test(config);
    size_t index;

    if (selected == NULL) {
        (void)printf("Default test: none\n");
    } else {
        (void)printf(
            "Default test: %s (top: %s)\n", selected->name, selected->top
        );
    }
    if (config->test_count == 0U) {
        (void)printf("Configured tests: none\n");
        return;
    }
    (void)printf("Configured tests:\n");
    for (index = 0U; index < config->test_count; index++) {
        const SolarTest *test = &config->tests[index];

        (void)printf("  %-12s top: %s", test->name, test->top);
        if (test == selected) (void)printf(" [default]");
        if (test->kind == SOLAR_TEST_GENERATED) (void)printf(" [generated]");
        if (test->cocotb != NULL ||
            (test->driver != NULL && strcmp(test->driver, "cocotb") == 0)) {
            (void)printf(" [cocotb]");
        }
        (void)printf("\n");
    }
}

SolarResult solar_cli_command_check(const char *start_path)
{
    SolarProject project;
    SolarResult result;

    solar_project_init(&project);
    result = solar_project_load(start_path, &project);
    if (result.status == SOLAR_STATUS_OK) {
        solar_cli_print_legacy_warning(&project);
        result = solar_project_validate(&project);
    }
    if (result.status == SOLAR_STATUS_OK) {
        if (project.discovery.ambiguous_testbench_files > 0U) {
            (void)fprintf(
                stderr,
                "solar: warning: %zu testbench file%s could not be mapped "
                "to a top module automatically\n"
                "hint: add an explicit [[test]] entry for ambiguous files\n",
                project.discovery.ambiguous_testbench_files,
                project.discovery.ambiguous_testbench_files == 1U ? "" : "s"
            );
        }
        (void)printf(
            "Project %s is valid (%zu RTL source%s, %zu test%s).\n",
            project.config.project.name,
            project.config.sources.rtl.count,
            project.config.sources.rtl.count == 1U ? "" : "s",
            project.config.test_count,
            project.config.test_count == 1U ? "" : "s"
        );
        (void)printf("Synthesis top: %s\n", project.config.synthesis.top);
        print_configured_tests(&project.config);
    }
    solar_project_free(&project);
    return result;
}

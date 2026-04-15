#include "core/imp_module.h"

#include "utils/logger.h"

static int template_init(const char* jsonConfig) {
    (void)jsonConfig;
    LOG_INFO("Template", "Template test init");
    return 0;
}

static void template_run(void) {
    LOG_INFO("Template", "Template test run");
}

static void template_cleanup(void) {
    LOG_INFO("Template", "Template test cleanup");
}

ImpModule imp_module_export = {
    .name = "Template",
    .version = "0.0.1",
    .init = template_init,
    .run = template_run,
    .cleanup = template_cleanup
};
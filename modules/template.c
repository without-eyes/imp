#include "core/imp_module.h"

#include "utils/logger.h"
#include "utils/ipc.h"

static int template_init(const char* jsonConfig) {
    (void)jsonConfig;
    LOG_INFO("Template", "Template test init");
    ipc_send_message("Memory", "INFO", "Template test init");
    return 0;
}

static void template_run(void) {
    LOG_INFO("Template", "Template test run");
    ipc_send_message("Template", "INFO", "Template test run");
}

static void template_cleanup(void) {
    LOG_INFO("Template", "Template test cleanup");
    ipc_send_message("Template", "INFO", "Template test cleanup");
}

ImpModule imp_module_export = {
    .name = "Template",
    .version = "0.0.2",
    .init = template_init,
    .run = template_run,
    .cleanup = template_cleanup
};
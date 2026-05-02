#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "core/imp_module.h"

#include <unistd.h>
#include <stdbool.h>
#include "utils/logger.h"
#include "utils/ipc.h"

// Internal defaults
#define DEFAULT_RUN_INTERVAL_SEC 5

static int template_init(const char* jsonConfig) {
    (void)jsonConfig;
    LOG_INFO("Template", "Initialized.");
    ipc_send_message("Template", "INFO", "Module initialized successfully.");
    return 0;
}

static void template_run(void) {
    LOG_INFO("Template", "Process started.");

    while (true) {
        sleep(DEFAULT_RUN_INTERVAL_SEC);
    }
}

static void template_cleanup(void) {
    LOG_INFO("Template", "Process shutting down.");
    ipc_send_message("Template", "INFO", "Module shutting down.");
}

ImpModule imp_module_export = {
    .name = "Template",
    .version = "0.0.2",
    .init = template_init,
    .run = template_run,
    .cleanup = template_cleanup
};
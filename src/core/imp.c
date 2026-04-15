#include "core/imp.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/types.h>
#include "core/imp_module.h"
#include "utils/logger.h"
#include "utils/cJSON.h"

static int read_config(char* jsonConfig, size_t jsonConfigSize) {
    FILE* fd = fopen(CONFIG_PATH, "rb");
    if (fd == NULL) {
        LOG_ERR("Core", "Could not open config file: %s. Exiting.", CONFIG_PATH);
        return -1;
    }

    size_t readSize = fread(jsonConfig, 1, jsonConfigSize - 1, fd);
    jsonConfig[readSize] = '\0';
    
    fclose(fd);
    return 0;
}

static int parse_and_load_modules(const char* jsonString) {
    cJSON *root = cJSON_Parse(jsonString);
    if (root == NULL) {
        LOG_ERR("Core", "Failed to parse JSON config: %s!", jsonString);
        return -1;
    }

    cJSON *modulesArray = cJSON_GetObjectItem(root, "modules");
    cJSON *moduleItem = NULL;

    cJSON_ArrayForEach(moduleItem, modulesArray) {
        cJSON *name = cJSON_GetObjectItem(moduleItem, "name");
        cJSON *enabled = cJSON_GetObjectItem(moduleItem, "enabled");
        cJSON *soPath = cJSON_GetObjectItem(moduleItem, "so_path");
        cJSON *moduleConfig = cJSON_GetObjectItem(moduleItem, "config");

        if (cJSON_IsTrue(enabled)) {
            LOG_INFO("Core", "Starting module [%s]...", name->valuestring);
            char *config_str = cJSON_PrintUnformatted(moduleConfig);
            
            pid_t pid = fork();
            if (pid < 0) {
                LOG_ERR("Core", "Fork failed for module [%s]", name->valuestring);
            } else if (pid == 0) {
                void* handle = dlopen(soPath->valuestring, RTLD_NOW);
                if (!handle) {
                    LOG_ERR("Core", "Failed to load %s: %s", soPath->valuestring, dlerror());
                    free(config_str);
                    exit(1);
                }

                ImpModule* module = (ImpModule*)dlsym(handle, IMP_MODULE_SYM);
                if (module == NULL) {
                    LOG_ERR("Core", "Symbol %s not found in %s", IMP_MODULE_SYM, soPath->valuestring);
                    dlclose(handle);
                    free(config_str);
                    exit(1);
                }

                LOG_INFO("Core", "Loaded [%s] successfully (v%s). Initializing...", module->name, module->version);

                if (module->init(config_str) == 0) {
                    module->run();
                } else {
                    LOG_ERR("Core", "Initialization [%s] failed!", module->name);
                }

                module->cleanup();
                dlclose(handle);
                free(config_str);
                
                exit(0); 
            }
            else {
                LOG_INFO("Core", "Module [%s] forked with PID: %d", name->valuestring, pid);
            }
            
            free(config_str);
        } else {
            LOG_NOTICE("Core", "Module [%s] is DISABLED. Skipping.", name->valuestring);
        }
    }

    cJSON_Delete(root);
    return 0;
}

int imp_run() {
    log_init(LOG_LEVEL_DEBUG);

    char jsonString[CONFIG_MAX_SIZE];
    if (read_config(jsonString, sizeof(jsonString)) != 0) {
        return -1;
    }

    if (parse_and_load_modules(jsonString)) {
        return -1;
    }

    return 0;
}

int imp_cleanup() {
    log_deinit();
    return 0;
}

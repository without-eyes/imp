#include "core/imp.h"

#include <stdio.h>
#include <stdlib.h>
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
            LOG_INFO("Core", "Module [%s] is ENABLED. Path: %s", name->valuestring, soPath->valuestring);
            
            char *config_str = cJSON_PrintUnformatted(moduleConfig);
            
            // TODO module fork and start
            
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

#include "core/imp.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "core/imp_module.h"
#include "utils/logger.h"
#include "utils/cJSON.h"

#define MAX_MODULES 16
#define MAX_CRASHES 3
#define RESTART_DELAY_SEC 5

typedef struct {
    pid_t pid;
    char name[64];
    char soPath[256];
    char* configString;
    int crashCount;
    bool enabled;
} ModuleRecord;

static ModuleRecord registry[MAX_MODULES];
static int moduleCount = 0;
volatile sig_atomic_t daemonActive = 1;

void handle_shutdown(int sig) {
    (void)sig;
    daemonActive = 0; 
}

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

static void spawn_module(int index, bool isRestart) {
    pid_t pid = fork();
    
    if (pid < 0) {
        LOG_ERR("Core", "Fork failed for module [%s]", registry[index].name);
        return;
    } 
    
    if (pid == 0) {
        if (isRestart) {
            sleep(RESTART_DELAY_SEC);
        }

        void* handle = dlopen(registry[index].soPath, RTLD_NOW);
        if (handle == NULL) {
            LOG_ERR("Core", "[%s] Failed to load: %s", registry[index].name, dlerror());
            exit(1);
        }

        ImpModule* module = (ImpModule*)dlsym(handle, IMP_MODULE_SYM);
        if (module == NULL) {
            LOG_ERR("Core", "[%s] Symbol not found", registry[index].name);
            dlclose(handle);
            exit(1); // TODO: Use EXIT_FAILURE/EXIT_SUCCESS
        }

        LOG_INFO("Core", "Loaded [%s] successfully (v%s)", module->name, module->version);

        if (module->init(registry[index].configString) == 0) {
            module->run();
        } else {
            LOG_ERR("Core", "Initialization [%s] failed!", module->name);
            exit(1);
        }

        module->cleanup();
        dlclose(handle);
        exit(0);
    } 
    else {
        registry[index].pid = pid;
        LOG_INFO("Core", "Module [%s] forked with PID: %d", registry[index].name, pid);
    }
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
        if (moduleCount >= MAX_MODULES) {
            break;
        }

        cJSON *enabled = cJSON_GetObjectItem(moduleItem, "enabled");
        if (!cJSON_IsTrue(enabled)) {
            LOG_NOTICE("Core", "Module [%s] is DISABLED. Skipping.", cJSON_GetObjectItem(moduleItem, "name")->valuestring);
            continue;
        }

        strncpy(registry[moduleCount].name, cJSON_GetObjectItem(moduleItem, "name")->valuestring, 63); // TODO: change magic numbers to macros
        strncpy(registry[moduleCount].soPath, cJSON_GetObjectItem(moduleItem, "so_path")->valuestring, 255);
        
        cJSON *moduleConfig = cJSON_GetObjectItem(moduleItem, "config");
        registry[moduleCount].configString = cJSON_PrintUnformatted(moduleConfig);
        registry[moduleCount].crashCount = 0;
        registry[moduleCount].enabled = true;

        spawn_module(moduleCount, false);
        moduleCount++;
    }

    cJSON_Delete(root);
    return 0;
}

static void supervisor_loop() {
    LOG_INFO("Core", "Entering Supervisor mode. Monitoring %d modules...", moduleCount);

    signal(SIGINT, handle_shutdown);
    signal(SIGTERM, handle_shutdown);
    
    while (daemonActive) {
        int status;
        pid_t deadPid = waitpid(-1, &status, 0); 
        
        if (deadPid == -1 && errno == EINTR) {
            continue; 
        }

        if (deadPid <= 0) {
            continue;
        }

        for (int i = 0; i < moduleCount; i++) {
            if (registry[i].enabled && registry[i].pid == deadPid) {
                registry[i].crashCount++;
                
                LOG_WARN("Core", "Module [%s] died (PID: %d). Crash count: %d/%d", 
                            registry[i].name, deadPid, registry[i].crashCount, MAX_CRASHES);

                if (registry[i].crashCount >= MAX_CRASHES) {
                    LOG_ERR("Core", "Module [%s] crashed too many times. Permanently disabled.", registry[i].name);
                    registry[i].enabled = false;
                } else {
                    LOG_INFO("Core", "Restarting [%s]...", registry[i].name);
                    spawn_module(i, true);
                }
                break;
            }
        }
    }
}

int imp_run() {
    log_init(LOG_FILE_PATH, LOG_LEVEL_DEBUG);

    char jsonString[CONFIG_MAX_SIZE];
    if (read_config(jsonString, sizeof(jsonString)) != 0) {
        return -1;
    }

    if (parse_and_load_modules(jsonString)) {
        return -1;
    }

    supervisor_loop();

    return 0;
}

int imp_cleanup() {
    LOG_INFO("Core", "Initiating shutdown. Cleaning up...");

    for (int i = 0; i < moduleCount; i++) {
        if (registry[i].enabled && registry[i].pid > 0) {
            LOG_DEBUG("Core", "Sending SIGTERM to module [%s] (PID: %d)", registry[i].name, registry[i].pid);
            kill(registry[i].pid, SIGTERM); 
        }

        if (registry[i].configString != NULL) {
            free(registry[i].configString);
            registry[i].configString = NULL;
        }
    }

    log_deinit();
    return 0;
}

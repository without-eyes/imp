#include "core/imp.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>

#include "core/imp_module.h"
#include "utils/logger.h"
#include "utils/ipc.h"
#include "utils/cJSON.h"

#define CONFIG_PATH "/etc/imp/imp.json"
#define CONFIG_MAX_SIZE 4096
#define MAX_MODULES 16
#define MAX_CRASHES 3
#define RESTART_DELAY_SEC 5

typedef struct {
    pid_t pid;
    char name[64];
    char type[16];    // "binary" або "script"
    char path[256];    // шлях до .so або .py
    char* configString;
    int crashCount;
    bool enabled;
} ModuleRecord;

static ModuleRecord registry[MAX_MODULES];
static int moduleCount = 0;
volatile sig_atomic_t daemonActive = 1;
static pthread_t broker_thread;

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

static void load_binary_module(int index) {
    void* handle = dlopen(registry[index].path, RTLD_NOW);
    if (handle == NULL) {
        LOG_ERR("Core", "[%s] Failed to load binary: %s", registry[index].name, dlerror());
        exit(1);
    }

    ImpModule* module = (ImpModule*)dlsym(handle, IMP_MODULE_SYM);
    if (module == NULL) {
        LOG_ERR("Core", "[%s] Symbol not found", registry[index].name);
        dlclose(handle);
        exit(1);
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

static void spawn_module(int index, bool isRestart) {
    pid_t pid = fork();
    
    if (pid < 0) {
        LOG_ERR("Core", "Fork failed for module [%s]", registry[index].name);
        return;
    } 
    
    if (pid == 0) { // Дочірній процес
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);

        if (isRestart) {
            sleep(RESTART_DELAY_SEC);
        }

        if (strcmp(registry[index].type, "binary") == 0) {
            load_binary_module(index);
        } 
        else if (strcmp(registry[index].type, "script") == 0) {
            LOG_INFO("Core", "Executing script module [%s] via python3", registry[index].name);
            
            // Запускаємо інтерпретатор Python
            char *args[] = {"python3", registry[index].path, NULL};
            execvp("python3", args);
            
            // Якщо execvp повернув керування — це помилка
            LOG_ERR("Core", "Failed to exec python3 for [%s]: %s", registry[index].name, strerror(errno));
            exit(1);
        } else {
            LOG_ERR("Core", "Unknown module type [%s] for [%s]", registry[index].type, registry[index].name);
            exit(1);
        }
    } 
    else { // Батьківський процес
        registry[index].pid = pid;
        LOG_INFO("Core", "Module [%s] (%s) started with PID: %d", 
                 registry[index].name, registry[index].type, pid);
    }
}

static int parse_and_load_modules(const char* jsonString) {
    cJSON *root = cJSON_Parse(jsonString);
    if (root == NULL) {
        LOG_ERR("Core", "Failed to parse JSON config!");
        return -1;
    }

    cJSON *modulesArray = cJSON_GetObjectItem(root, "modules");
    cJSON *moduleItem = NULL;

    cJSON_ArrayForEach(moduleItem, modulesArray) {
        if (moduleCount >= MAX_MODULES) break;

        cJSON *enabled = cJSON_GetObjectItem(moduleItem, "enabled");
        if (!cJSON_IsTrue(enabled)) continue;

        // Отримуємо базові поля
        cJSON *name = cJSON_GetObjectItem(moduleItem, "name");
        cJSON *type = cJSON_GetObjectItem(moduleItem, "type");
        cJSON *path = cJSON_GetObjectItem(moduleItem, "path");

        if (!name || !type || !path) {
            LOG_WARN("Core", "Skipping incomplete module definition in config.");
            continue;
        }

        strncpy(registry[moduleCount].name, name->valuestring, 63);
        strncpy(registry[moduleCount].type, type->valuestring, 15);
        strncpy(registry[moduleCount].path, path->valuestring, 255);
        
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

static void* ipc_broker_loop(void* arg) {
    (void)arg;
    int server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sock < 0) return NULL;

    unlink(IMP_SOCKET_PATH);
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, IMP_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(server_sock);
        return NULL;
    }
    
    listen(server_sock, 10);
    LOG_INFO("Broker", "IPC Message Bus active on %s", IMP_SOCKET_PATH);

    while (daemonActive) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_sock, &readfds);
        struct timeval tv = {1, 0};

        if (select(server_sock + 1, &readfds, NULL, NULL, &tv) > 0) {
            int client_sock = accept(server_sock, NULL, NULL);
            if (client_sock >= 0) {
                char buffer[1024] = {0};
                if (read(client_sock, buffer, sizeof(buffer) - 1) > 0) {
                    LOG_NOTICE("Broker", "Event: %s", buffer);
                }
                close(client_sock);
            }
        }
    }

    close(server_sock);
    unlink(IMP_SOCKET_PATH);
    return NULL;
}

static void supervisor_loop() {
    LOG_INFO("Core", "Supervisor active. Monitoring %d modules.", moduleCount);
    signal(SIGINT, handle_shutdown);
    signal(SIGTERM, handle_shutdown);
    
    while (daemonActive) {
        int status;
        pid_t deadPid = waitpid(-1, &status, 0); 
        
        if (deadPid <= 0) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < moduleCount; i++) {
            if (registry[i].enabled && registry[i].pid == deadPid) {
                registry[i].crashCount++;
                LOG_WARN("Core", "Module [%s] died. Crashes: %d/%d", 
                         registry[i].name, registry[i].crashCount, MAX_CRASHES);

                if (registry[i].crashCount >= MAX_CRASHES) {
                    LOG_ERR("Core", "Module [%s] disabled (too many crashes).", registry[i].name);
                    registry[i].enabled = false;
                } else {
                    spawn_module(i, true);
                }
                break;
            }
        }
    }
}

void imp_daemonize(void) {
    pid_t pid = fork();
    if (pid != 0) exit(pid < 0 ? 1 : 0);
    setsid();
    signal(SIGHUP, SIG_IGN);
    pid = fork();
    if (pid != 0) exit(pid < 0 ? 1 : 0);
    umask(0);
    chdir("/");
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
}

int imp_run() {
    log_init(LOG_FILE_PATH, LOG_LEVEL_DEBUG);
    char jsonString[CONFIG_MAX_SIZE];
    if (read_config(jsonString, sizeof(jsonString)) != 0) return -1;
    if (parse_and_load_modules(jsonString)) return -1;
    pthread_create(&broker_thread, NULL, ipc_broker_loop, NULL);
    supervisor_loop();
    return 0;
}

int imp_cleanup() {
    LOG_INFO("Core", "Cleaning up...");
    daemonActive = 0;
    pthread_join(broker_thread, NULL);
    for (int i = 0; i < moduleCount; i++) {
        if (registry[i].pid > 0) kill(registry[i].pid, SIGTERM);
        if (registry[i].configString) free(registry[i].configString);
    }
    log_deinit();
    return 0;
}

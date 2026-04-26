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

#ifndef CONFIG_PATH
#define CONFIG_PATH "/etc/imp/imp.json"
#endif

#ifndef DEFAULT_LOG_FILE
#define DEFAULT_LOG_FILE "/var/log/imp/imp.log"
#endif

#ifndef DEFAULT_PID_FILE
#define DEFAULT_PID_FILE "/var/run/imp.pid"
#endif

#define CONFIG_MAX_SIZE 2048
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
static pthread_t broker_thread;

static char core_log_file[256] = DEFAULT_LOG_FILE;
static char core_pid_file[256] = DEFAULT_PID_FILE;

void handle_shutdown(int sig) {
    (void)sig;
    daemonActive = 0; 
}

static log_level_t parse_core_config(const char* jsonString) {
    log_level_t level = LOG_LEVEL_INFO;
    cJSON *root = cJSON_Parse(jsonString);
    
    if (root != NULL) {
        cJSON *lvl = cJSON_GetObjectItem(root, "log_level");
        if (cJSON_IsString(lvl)) {
            if (strcmp(lvl->valuestring, "DEBUG") == 0) level = LOG_LEVEL_DEBUG;
            else if (strcmp(lvl->valuestring, "INFO") == 0) level = LOG_LEVEL_INFO;
            else if (strcmp(lvl->valuestring, "NOTICE") == 0) level = LOG_LEVEL_NOTICE;
            else if (strcmp(lvl->valuestring, "WARNING") == 0) level = LOG_LEVEL_WARNING;
            else if (strcmp(lvl->valuestring, "ERR") == 0) level = LOG_LEVEL_ERROR;
            else if (strcmp(lvl->valuestring, "CRITICAL") == 0) level = LOG_LEVEL_CRITICAL;
        }

        cJSON *log_file = cJSON_GetObjectItem(root, "log_file");
        if (cJSON_IsString(log_file)) {
            strncpy(core_log_file, log_file->valuestring, sizeof(core_log_file) - 1);
        }

        cJSON *pid_file = cJSON_GetObjectItem(root, "pid_file");
        if (cJSON_IsString(pid_file)) {
            strncpy(core_pid_file, pid_file->valuestring, sizeof(core_pid_file) - 1);
        }

        cJSON_Delete(root);
    }
    return level;
}

static int read_config(char* jsonConfig, size_t jsonConfigSize) {
    FILE* fd = fopen(CONFIG_PATH, "rb");
    if (fd == NULL) {
        return -1;
    }

    size_t readSize = fread(jsonConfig, 1, jsonConfigSize - 1, fd);
    jsonConfig[readSize] = '\0';
    
    fclose(fd);
    return 0;
}

static void write_pid_file() {
    FILE *fp = fopen(core_pid_file, "w");
    if (fp) {
        fprintf(fp, "%d\n", getpid());
        fclose(fp);
    } else {
        LOG_WARN("Core", "Could not write PID file to %s", core_pid_file);
    }
}

static void remove_pid_file() {
    unlink(core_pid_file);
}

static void spawn_module(int index, bool isRestart) {
    pid_t pid = fork();
    
    if (pid < 0) {
        LOG_ERR("Core", "Fork failed for module [%s]", registry[index].name);
        return;
    } 
    
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);

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

        strncpy(registry[moduleCount].name, cJSON_GetObjectItem(moduleItem, "name")->valuestring, 63);
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

static void* ipc_broker_loop(void* arg) {
    (void)arg;

    int server_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_sock < 0) {
        LOG_ERR("Broker", "Failed to create IPC socket.");
        return NULL;
    }

    unlink(IMP_SOCKET_PATH);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, IMP_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        LOG_ERR("Broker", "Failed to bind IPC socket.");
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

        int activity = select(server_sock + 1, &readfds, NULL, NULL, &tv);
        
        if (activity > 0 && FD_ISSET(server_sock, &readfds)) {
            int client_sock = accept(server_sock, NULL, NULL);
            if (client_sock >= 0) {
                char buffer[1024] = {0};
                ssize_t bytes_read = read(client_sock, buffer, sizeof(buffer) - 1);
                
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    LOG_NOTICE("Broker", "Received Event: %s", buffer);
                }
                close(client_sock);
            }
        }
    }

    close(server_sock);
    unlink(IMP_SOCKET_PATH);
    LOG_INFO("Broker", "IPC Message Bus shut down.");
    return NULL;
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

void imp_daemonize(void) {
    pid_t pid;

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    else if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) exit(EXIT_FAILURE);

    signal(SIGHUP, SIG_IGN);

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    else if (pid > 0) exit(EXIT_SUCCESS); 

    umask(0);

    if (chdir("/") < 0) exit(EXIT_FAILURE);

    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }
}

int imp_run() {
    char jsonString[CONFIG_MAX_SIZE];
    
    if (read_config(jsonString, sizeof(jsonString)) != 0) {
        log_init(core_log_file, LOG_LEVEL_INFO);
        LOG_ERR("Core", "Could not open config file: %s. Exiting.", CONFIG_PATH);
        return -1;
    }

    log_level_t current_level = parse_core_config(jsonString);
    log_init(core_log_file, current_level);

    write_pid_file();

    if (parse_and_load_modules(jsonString)) {
        return -1;
    }

    if (pthread_create(&broker_thread, NULL, ipc_broker_loop, NULL) != 0) {
        LOG_ERR("Core", "Failed to start IPC Broker thread.");
        return -1;
    }

    supervisor_loop();

    return 0;
}

int imp_cleanup() {
    LOG_INFO("Core", "Initiating shutdown. Cleaning up...");

    pthread_join(broker_thread, NULL);

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

    remove_pid_file();
    log_deinit();
    return 0;
}

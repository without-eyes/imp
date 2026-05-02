#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "core/imp_module.h"
#include "utils/logger.h"
#include "utils/ipc.h"
#include "utils/cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/resource.h>
#include <ctype.h>
#include <stdbool.h>

// Math and conversions
#define BYTES_IN_KB 1024

// Priorities
#define LOW_PRIORITY 19
#define HIGH_PRIORITY -5

// Internal limits
#define MAX_IMPORTANT 20
#define MAX_PROCESS_NAME_LEN 256
#define MAX_PATH_LEN 512

// Internal defaults
#define DEFAULT_CHECK_INTERVAL_SEC_VALUE 10
#define DEFAULT_CPU_THRESHOLD 80
#define DEFAULT_RAM_THRESHOLD_KB (1024 * BYTES_IN_KB)
#define DEFAULT_IMPORTANT_PROCESS_COUNT 0

static int check_interval_sec = DEFAULT_CHECK_INTERVAL_SEC_VALUE;
static int cpu_threshold = DEFAULT_CPU_THRESHOLD;
static long ram_threshold_kb = DEFAULT_RAM_THRESHOLD_KB;
static char important_procs[MAX_IMPORTANT][MAX_PROCESS_NAME_LEN];
static int important_count = DEFAULT_IMPORTANT_PROCESS_COUNT;

static int prioritizer_init(const char* jsonConfig) {
    cJSON *root = cJSON_Parse(jsonConfig);
    if (root == NULL) {
        LOG_ERR("Prioritizer", "Failed to parse JSON config.");
        return -1;
    }

    cJSON *interval = cJSON_GetObjectItem(root, "check_interval_sec");
    if (cJSON_IsNumber(interval)) {
        check_interval_sec = interval->valueint;
    }

    cJSON *cpu = cJSON_GetObjectItem(root, "cpu_threshold_percent");
    if (cJSON_IsNumber(cpu)) {
        cpu_threshold = cpu->valueint;
    }

    cJSON *ram = cJSON_GetObjectItem(root, "ram_threshold_mb");
    if (cJSON_IsNumber(ram)) {
        ram_threshold_kb = (long)ram->valueint * BYTES_IN_KB;
    }

    cJSON *list = cJSON_GetObjectItem(root, "important_processes");
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, list) {
        if (important_count < MAX_IMPORTANT) {
            strncpy(important_procs[important_count++], item->valuestring, MAX_PROCESS_NAME_LEN - 1);
        }
    }

    cJSON_Delete(root);
    LOG_INFO("Prioritizer", "Initialized. Monitoring system resources every %ds.", check_interval_sec);
    return 0;
}

static bool is_important(const char* name) {
    for (int i = 0; i < important_count; i++) {
        if (strcmp(name, important_procs[i]) == 0) {
            return true;
        }
    }
    return false;
}

static void handle_heavy_process(pid_t pid, const char* name, long rss_kb) {
    if (is_important(name)) {
        if (getpriority(PRIO_PROCESS, pid) > HIGH_PRIORITY) {
            if (setpriority(PRIO_PROCESS, pid, HIGH_PRIORITY) == 0) {
                LOG_INFO("Prioritizer", "Boosted priority for important process [%s] (PID: %d)", name, pid);
            } else {
                LOG_DEBUG("Prioritizer", "Failed to boost priority for [%s] (PID: %d)", name, pid);
            }
        }
        return;
    }

    if (rss_kb > ram_threshold_kb) {
        LOG_WARN("Prioritizer", "Process [%s] (PID: %d) uses too much RAM: %ld MB. Lowering priority.", 
                    name, pid, rss_kb / BYTES_IN_KB);
 
        char ipc_msg[MAX_IPC_MESSAGE_LEN];
        snprintf(ipc_msg, sizeof(ipc_msg), "Process [%s] (PID: %d) uses too much RAM: %ld MB", name, pid, rss_kb / BYTES_IN_KB);
        ipc_send_message("Prioritizer", "WARNING", ipc_msg);

        setpriority(PRIO_PROCESS, pid, LOW_PRIORITY);
    }
}

static void scan_processes() {
    DIR* dir = opendir("/proc");
    if (dir == NULL) {
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!isdigit(entry->d_name[0])) {
            continue;
        }

        pid_t pid = atoi(entry->d_name);
        char path[MAX_PATH_LEN], comm[MAX_PROCESS_NAME_LEN];
        long rss = 0;

        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        FILE* f = fopen(path, "r");
        if (f != NULL) {
            if (fscanf(f, "%*d (%255[^)]) %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*d %*d %*d %*d %*d %*u %*u %ld", 
                       comm, &rss) == 2) {
                
                long rss_kb = rss * sysconf(_SC_PAGESIZE) / BYTES_IN_KB;
                handle_heavy_process(pid, comm, rss_kb);
            }
            fclose(f);
        }
    }
    closedir(dir);
}

static void prioritizer_run(void) {
    LOG_INFO("Prioritizer", "Resource monitor active.");
    while (true) {
        scan_processes();
        sleep(check_interval_sec);
    }
}

static void prioritizer_cleanup(void) {
    LOG_INFO("Prioritizer", "Shutting down.");
}

ImpModule imp_module_export = {
    .name = "Prioritizer",
    .version = "1.0.0",
    .init = prioritizer_init,
    .run = prioritizer_run,
    .cleanup = prioritizer_cleanup
};
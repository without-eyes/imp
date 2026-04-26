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

#define MAX_IMPORTANT 20

static int check_interval_sec = 10;
static int cpu_threshold = 80;
static long ram_threshold_kb = 1024 * 1024;
static char important_procs[MAX_IMPORTANT][64];
static int important_count = 0;

static int prioritizer_init(const char* jsonConfig) {
    cJSON *root = cJSON_Parse(jsonConfig);
    if (root == NULL) {
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
        ram_threshold_kb = (long)ram->valueint * 1024;
    }

    cJSON *list = cJSON_GetObjectItem(root, "important_processes");
    important_count = 0;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, list) {
        if (important_count < MAX_IMPORTANT) {
            strncpy(important_procs[important_count++], item->valuestring, 63);
        }
    }

    cJSON_Delete(root);
    LOG_INFO("Prioritizer", "Initialized. Monitoring system resources every %ds.", check_interval_sec);
    return 0;
}

static int is_important(const char* name) {
    for (int i = 0; i < important_count; i++) {
        if (strcmp(name, important_procs[i]) == 0) return 1;
    }
    return 0;
}

static void handle_heavy_process(pid_t pid, const char* name, long rss_kb) {
    if (is_important(name)) {
        if (getpriority(PRIO_PROCESS, pid) > -5) {
            if (setpriority(PRIO_PROCESS, pid, -5) == 0) {
                LOG_INFO("Prioritizer", "Boosted priority for important process [%s] (PID: %d)", name, pid);
            } else {
                LOG_DEBUG("Prioritizer", "Failed to boost priority for [%s] (PID: %d)", name, pid);
            }
        }
        return;
    }

    if (rss_kb > ram_threshold_kb) {
        LOG_WARN("Prioritizer", "Process [%s] (PID: %d) uses too much RAM: %ld MB. Lowering priority.", 
                    name, pid, rss_kb / 1024);
 
        char ipc_msg[256];
        snprintf(ipc_msg, sizeof(ipc_msg), "Process [%s] (PID: %d) uses too much RAM: %ld MB", name, pid, rss_kb / 1024);
        ipc_send_message("Prioritizer", "WARNING", ipc_msg);

        setpriority(PRIO_PROCESS, pid, 19);
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
        char path[512], comm[256];
        long rss = 0;

        snprintf(path, sizeof(path), "/proc/%d/stat", pid);
        FILE* f = fopen(path, "r");
        if (f != NULL) {
            if (fscanf(f, "%*d (%255[^)]) %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*d %*d %*d %*d %*d %*u %*u %ld", 
                       comm, &rss) == 2) {
                
                long rss_kb = rss * sysconf(_SC_PAGESIZE) / 1024;
                handle_heavy_process(pid, comm, rss_kb);
            }
            fclose(f);
        }
    }
    closedir(dir);
}

static void prioritizer_run(void) {
    LOG_INFO("Prioritizer", "Resource monitor active.");
    while (1) {
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
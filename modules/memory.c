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
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <fnmatch.h>
#include <stdbool.h>

// Internal limits
#define MAX_TARGETS 10
#define MAX_MASK_LEN 64
#define MAX_FILENAME_LEN 256
#define MAX_PATH_LEN 512
#define MAX_FULL_PATH_LEN (MAX_PATH_LEN + MAX_FILENAME_LEN)

// Math and Time constants
#define PERCENT_MULTIPLIER 100.0
#define SECONDS_PER_HOUR   3600.0
#define HOURS_PER_DAY      24.0
#define SECONDS_PER_DAY    (SECONDS_PER_HOUR * HOURS_PER_DAY)

// Internal defaults
#define DEFAULT_TARGET_COUNT 0
#define DEFAULT_CHECK_INTERVAL_SEC_VALUE 3600
#define DEFAULT_CRITICAL_DISK_USAGE_PERCENT 90

typedef struct {
    char path[MAX_PATH_LEN];
    char mask[MAX_MASK_LEN];
    int max_age_days;
} CleanTarget;

static CleanTarget targets[MAX_TARGETS];
static int target_count = DEFAULT_TARGET_COUNT;
static int check_interval_sec = DEFAULT_CHECK_INTERVAL_SEC_VALUE;
static int critical_disk_usage_percent = DEFAULT_CRITICAL_DISK_USAGE_PERCENT;

static int memory_init(const char* jsonConfig) {
    cJSON *root = cJSON_Parse(jsonConfig);
    if (root == NULL) {
        LOG_ERR("Memory", "Failed to parse JSON config.");
        return -1;
    }

    cJSON *interval = cJSON_GetObjectItem(root, "check_interval_sec");
    if (cJSON_IsNumber(interval)) {
        check_interval_sec = interval->valueint;
    }

    cJSON *disk_limit = cJSON_GetObjectItem(root, "critical_disk_usage_percent");
    if (cJSON_IsNumber(disk_limit)) {
        critical_disk_usage_percent = disk_limit->valueint;
    }

    cJSON *targets_array = cJSON_GetObjectItem(root, "targets");
    cJSON *target_item = NULL;
    
    target_count = 0;
    cJSON_ArrayForEach(target_item, targets_array) {
        if (target_count >= MAX_TARGETS) {
            LOG_WARN("Memory", "Max targets reached (%d). Ignoring the rest.", MAX_TARGETS);
            break;
        }
        
        cJSON *path = cJSON_GetObjectItem(target_item, "path");
        cJSON *mask = cJSON_GetObjectItem(target_item, "mask");
        cJSON *days = cJSON_GetObjectItem(target_item, "max_age_days");
        
        if (cJSON_IsString(path) && cJSON_IsString(mask) && cJSON_IsNumber(days)) {
            strncpy(targets[target_count].path, path->valuestring, sizeof(targets[0].path) - 1);
            strncpy(targets[target_count].mask, mask->valuestring, sizeof(targets[0].mask) - 1);
            targets[target_count].max_age_days = days->valueint;
            target_count++;
        }
    }

    cJSON_Delete(root);
    LOG_INFO("Memory", "Initialized. Monitoring %d targets. Interval: %ds", target_count, check_interval_sec);
    return 0;
}

static void check_disk_space(const char* path) {
    struct statvfs vfs;
    if (statvfs(path, &vfs) == 0) {
        double total_blocks = vfs.f_blocks;
        double free_blocks = vfs.f_bavail;
        
        if (total_blocks == 0) {
            return;
        }

        double used_percent = ((total_blocks - free_blocks) / total_blocks) * PERCENT_MULTIPLIER;

        if (used_percent >= critical_disk_usage_percent) {
            LOG_CRITICAL("Memory", "DISK ALMOST FULL on [%s]! Usage: %.1f%% (Limit: %d%%)", 
                         path, used_percent, critical_disk_usage_percent);
            
            char ipc_msg[MAX_IPC_MESSAGE_LEN];
            snprintf(ipc_msg, sizeof(ipc_msg), "DISK ALMOST FULL on [%s]! Usage: %.1f%%", path, used_percent);
            ipc_send_message("Memory", "CRITICAL", ipc_msg);
        } else {
            LOG_DEBUG("Memory", "Disk usage on [%s]: %.1f%%", path, used_percent);
        }
    }
}

static void clean_target(CleanTarget* target) {
    check_disk_space(target->path);

    DIR *dir = opendir(target->path);
    if (dir == NULL) {
        LOG_WARN("Memory", "Cannot access directory: %s", target->path);
        return;
    }

    struct dirent *entry;
    struct stat file_stat;
    char filepath[MAX_FULL_PATH_LEN];
    time_t current_time = time(NULL);
    int deleted_count = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (fnmatch(target->mask, entry->d_name, FNM_CASEFOLD) != 0) {
            continue;
        }

        snprintf(filepath, sizeof(filepath), "%s/%s", target->path, entry->d_name);

        if (stat(filepath, &file_stat) == 0) {
            if (S_ISREG(file_stat.st_mode)) { 
                double age_sec = difftime(current_time, file_stat.st_mtime);
                double max_age_sec = target->max_age_days * SECONDS_PER_DAY;

                if (age_sec > max_age_sec) {
                    LOG_DEBUG("Memory", "Deleting old file: %s (Age: %.1f days)", filepath, age_sec / SECONDS_PER_DAY);
                    if (unlink(filepath) == 0) {
                        deleted_count++;
                    } else {
                        LOG_WARN("Memory", "Failed to delete file: %s", filepath);
                    }
                }
            }
        }
    }

    closedir(dir);
    
    if (deleted_count > 0) {
        LOG_INFO("Memory", "Cleaned %d old files from [%s] using mask [%s]", 
                 deleted_count, target->path, target->mask);
    }
}

static void memory_run(void) {
    LOG_INFO("Memory", "Process started.");
    
    while (true) {
        for (int i = 0; i < target_count; i++) {
            clean_target(&targets[i]);
        }
        sleep(check_interval_sec); 
    }
}

static void memory_cleanup(void) {
    LOG_INFO("Memory", "Process shutting down.");
}

ImpModule imp_module_export = {
    .name = "Memory",
    .version = "1.0.0",
    .init = memory_init,
    .run = memory_run,
    .cleanup = memory_cleanup
};
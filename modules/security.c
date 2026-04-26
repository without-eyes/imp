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
#include <sys/stat.h>

#ifndef TCP_FILE
#define TCP_FILE "/proc/net/tcp"
#endif

#define MAX_FILES 10

static char critical_files[MAX_FILES][256];
static int file_count = 0;
static int check_interval_sec = 3600;

static int security_init(const char* jsonConfig) {
    cJSON *root = cJSON_Parse(jsonConfig);
    if (root == NULL) {
        return -1;
    }

    cJSON *interval = cJSON_GetObjectItem(root, "check_interval_sec");
    if (cJSON_IsNumber(interval)) {
        check_interval_sec = interval->valueint;
    }

    cJSON *files_array = cJSON_GetObjectItem(root, "critical_files");
    cJSON *file_item = NULL;
    
    file_count = 0;
    cJSON_ArrayForEach(file_item, files_array) {
        if (file_count >= MAX_FILES) {
            break;
        }

        if (cJSON_IsString(file_item)) {
            strncpy(critical_files[file_count], file_item->valuestring, 255);
            file_count++;
        }
    }

    cJSON_Delete(root);
    LOG_INFO("Security", "Initialized. Monitoring %d critical files. Interval: %ds", file_count, check_interval_sec);
    return 0;
}

static void check_critical_files() {
    struct stat st;
    for (int i = 0; i < file_count; i++) {
        if (stat(critical_files[i], &st) == 0) {
            if (st.st_mode & S_IROTH) {
                char ipc_msg[512];
                if (strstr(critical_files[i], "shadow") != NULL || strstr(critical_files[i], "sudoers") != NULL) {
                    LOG_CRITICAL("Security", "VULNERABILITY: Critical file is world-readable: %s", critical_files[i]);
                    snprintf(ipc_msg, sizeof(ipc_msg), "VULNERABILITY: Critical file is world-readable: %s", critical_files[i]);
                    ipc_send_message("Security", "CRITICAL", ipc_msg);
                } else {
                    LOG_WARN("Security", "Critical file is world-readable: %s", critical_files[i]);
                    snprintf(ipc_msg, sizeof(ipc_msg), "WARNING: Critical file is world-readable: %s", critical_files[i]);
                    ipc_send_message("Security", "WARNING", ipc_msg);
                }
            }
        } else {
            LOG_ERR("Security", "Cannot access critical file: %s", critical_files[i]);
        }
    }
}

static void check_network_tcp() {
    FILE *fp = fopen(TCP_FILE, "r");
    if (fp == NULL) {
        LOG_ERR("Security", "Cannot open %s", TCP_FILE);
        return;
    }

    char line[256];
    int line_num = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        if (line_num == 1) {
            continue;
        }

        char local_addr[64], rem_addr[64];
        int state;
        
        if (sscanf(line, "%*d: %63s %63s %d", local_addr, rem_addr, &state) == 3) {
            
            unsigned int local_port;
            sscanf(local_addr, "%*[^:]:%X", &local_port);

            if (state == 0x0A) {
                LOG_DEBUG("Security", "Open port detected (LISTEN): %d", local_port);
            }
            
            if (state == 0x01 && local_port == 22) {
                unsigned int rem_ip, rem_port;
                sscanf(rem_addr, "%X:%X", &rem_ip, &rem_port);
                
                unsigned char ip[4];
                ip[0] = rem_ip & 0xFF;
                ip[1] = (rem_ip >> 8) & 0xFF;
                ip[2] = (rem_ip >> 16) & 0xFF;
                ip[3] = (rem_ip >> 24) & 0xFF;

                LOG_NOTICE("Security", "Active SSH connection detected from IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
                
                char ipc_msg[256];
                snprintf(ipc_msg, sizeof(ipc_msg), "Active SSH connection detected from IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
                ipc_send_message("Security", "NOTICE", ipc_msg);
            }
        }
    }
    fclose(fp);
}

static void security_run(void) {
    LOG_INFO("Security", "Scanner active.");
    
    while (1) {
        check_critical_files();
        check_network_tcp();
        
        sleep(check_interval_sec); 
    }
}

static void security_cleanup(void) {
    LOG_INFO("Security", "Scanner shutting down.");
}

ImpModule imp_module_export = {
    .name = "Security",
    .version = "1.0.0",
    .init = security_init,
    .run = security_run,
    .cleanup = security_cleanup
};
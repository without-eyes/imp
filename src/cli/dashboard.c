#include "cli/dashboard.h"
#include "utils/logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>

#define DASHBOARD_SOCKET_PATH "/tmp/imp_dashboard.sock"
#define MAX_LOGS 500
#define MAX_LOGS_PER_MODULE 5

volatile sig_atomic_t dashboard_active = 1;

static char recent_logs[MAX_LOGS][512];
static int log_count = 0;

typedef struct {
    char time[16];
    char module[32];
    char level[16];
    char message[256];
    int is_valid;
} ParsedLog;

void handle_dashboard_exit(int sig) {
    (void)sig;
    dashboard_active = 0;
}

static void print_header() {
    printf("\033[1;37mI.M.P. v1.0.0\033[0m\033[K\n\n");
}

static void print_uptime() {
    FILE *fp = fopen("/proc/uptime", "r");
    double uptime_sec = 0;
    if (fp) {
        fscanf(fp, "%lf", &uptime_sec);
        fclose(fp);
    }
    int h = (int)uptime_sec / 3600;
    int m = ((int)uptime_sec % 3600) / 60;
    
    printf("\n\033[1mSystem Uptime:\033[0m %02dh %02dm\033[K\n", h, m);
}

static double get_cpu_temp() {
    FILE *fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (fp) {
        long millidegrees;
        if (fscanf(fp, "%ld", &millidegrees) == 1) {
            fclose(fp);
            return millidegrees / 1000.0;
        }
        fclose(fp);
    }
    return -1.0; 
}

static double get_ram_usage() {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp == NULL) {
        return 0.0;
    }

    char line[256];
    long total_ram = 0, available_ram = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "MemTotal:", 9) == 0) sscanf(line, "%*s %ld", &total_ram);
        if (strncmp(line, "MemAvailable:", 13) == 0) sscanf(line, "%*s %ld", &available_ram);
    }
    fclose(fp);

    if (total_ram == 0) {
        return 0.0;
    }

    return ((double)(total_ram - available_ram) / total_ram) * 100.0;
}

static double get_swap_usage() {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp == NULL) {
        return 0.0;
    }

    char line[256];
    long total_swap = 0, free_swap = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "SwapTotal:", 10) == 0) sscanf(line, "%*s %ld", &total_swap);
        if (strncmp(line, "SwapFree:", 9) == 0) sscanf(line, "%*s %ld", &free_swap);
    }

    fclose(fp);
    
    if (total_swap == 0) {
        return 0.0;
    }
    
    return ((double)(total_swap - free_swap) / total_swap) * 100.0;
}

static double get_cpu_usage() {
    static long prev_idle = 0, prev_total = 0;
    FILE *fp = fopen("/proc/stat", "r");
    if (fp == NULL) {
        return 0.0;
    }

    long user, nice, system, idle, iowait, irq, softirq;
    fscanf(fp, "cpu %ld %ld %ld %ld %ld %ld %ld", &user, &nice, &system, &idle, &iowait, &irq, &softirq);
    fclose(fp);

    long total = user + nice + system + idle + iowait + irq + softirq;
    long total_diff = total - prev_total;
    long idle_diff = idle - prev_idle;

    prev_total = total;
    prev_idle = idle;

    if (total_diff == 0) {
        return 0.0;
    }

    return ((double)(total_diff - idle_diff) / total_diff) * 100.0;
}

static void print_bar(const char* label, double percent, const char* color, const char* extra_text) {
    int bar_width = 40;
    int filled = (int)((percent / 100.0) * bar_width);

    printf("\033[1m%-10s\033[0m [", label);
    printf("%s", color);
    for (int i = 0; i < bar_width; i++) {
        if (i < filled) printf("■");
        else printf(" ");
    }

    if (extra_text) {
        printf("\033[0m] %5.1f%%  \033[33m%s\033[0m\033[K\n", percent, extra_text);
    } else {
        printf("\033[0m] %5.1f%%\033[K\n", percent);
    }
}

static void load_initial_logs() {
    FILE *fp = fopen(LOG_FILE_PATH, "r");
    if (fp == NULL) {
        return;
    }
    
    char buffer[512];
    while (fgets(buffer, sizeof(buffer), fp)) {
        strncpy(recent_logs[log_count % MAX_LOGS], buffer, 511);
        log_count++;
    }

    fclose(fp);
}

static void parse_log_line(const char* raw_line, ParsedLog* parsed) {
    parsed->is_valid = 0;
    if (strncmp(raw_line, "[LIVE IPC]", 10) == 0) {
        char* src = strstr(raw_line, "\"source\":\"");
        char* lvl = strstr(raw_line, "\"level\":\"");
        char* msg = strstr(raw_line, "\"message\":\"");
        if (src && lvl && msg) {
            sscanf(src + 10, "%31[^\"]", parsed->module);
            sscanf(lvl + 9, "%15[^\"]", parsed->level);
            sscanf(msg + 11, "%255[^\"]", parsed->message);
            strcpy(parsed->time, "LIVE");
            parsed->is_valid = 1;
        }
    } else {
        char date[32], time[32], pid[32], mod[32], file[64], lvl[16];
        if (sscanf(raw_line, "[%s %[^]]][%[^]]][%[^]]][%[^]]][%[^]]] %[^\n]", 
                   date, time, pid, mod, file, lvl, parsed->message) >= 7) {
            time[8] = '\0'; 
            strcpy(parsed->time, time);
            strcpy(parsed->module, mod);
            strcpy(parsed->level, lvl);
            parsed->is_valid = 1;
        }
    }
}

static void print_recent_logs() {
    if (log_count == 0) {
        printf("  No recent actions.\033[K\n");
        return;
    }

    int start = log_count > MAX_LOGS ? log_count % MAX_LOGS : 0;
    int total = log_count > MAX_LOGS ? MAX_LOGS : log_count;

    ParsedLog parsed_array[MAX_LOGS];
    for (int i = 0; i < total; i++) {
        parse_log_line(recent_logs[(start + i) % MAX_LOGS], &parsed_array[i]);
    }

    char unique_modules[10][32];
    int unique_count = 0;

    for (int i = 0; i < total; i++) {
        if (!parsed_array[i].is_valid) continue;
        int found = 0;
        for (int j = 0; j < unique_count; j++) {
            if (strcmp(unique_modules[j], parsed_array[i].module) == 0) {
                found = 1; break;
            }
        }
        if (!found && unique_count < 10) {
            strcpy(unique_modules[unique_count++], parsed_array[i].module);
        }
    }

    for (int m = 0; m < unique_count; m++) {
        printf("\n\033[1;36m[ MODULE: %s ]\033[0m ", unique_modules[m]);
        int dash_count = 55 - strlen(unique_modules[m]);
        for(int d=0; d<dash_count; d++) printf("-");
        printf("\033[K\n"); 
        
        int mod_log_count = 0;
        for (int i = 0; i < total; i++) {
            if (parsed_array[i].is_valid && strcmp(parsed_array[i].module, unique_modules[m]) == 0) {
                mod_log_count++;
            }
        }

        int skip_count = mod_log_count > MAX_LOGS_PER_MODULE ? mod_log_count - MAX_LOGS_PER_MODULE : 0;

        for (int i = 0; i < total; i++) {
            if (parsed_array[i].is_valid && strcmp(parsed_array[i].module, unique_modules[m]) == 0) {
                if (skip_count > 0) {
                    skip_count--;
                    continue; 
                }

                const char* color = "\033[2m"; 
                if (strcmp(parsed_array[i].level, "CRITICAL") == 0) {
                    color = "\033[1;31m";
                } else if (strcmp(parsed_array[i].level, "WARNING") == 0) {
                    color = "\033[1;33m";
                } else if (strcmp(parsed_array[i].level, "NOTICE") == 0) {
                    color = "\033[1;34m";
                } else if (strcmp(parsed_array[i].level, "INFO") == 0) {
                    color = "\033[0m";
                }

                printf("  \033[2m[%s]\033[0m %s[%-8s] %s\033[0m\033[K\n", 
                       parsed_array[i].time, color, parsed_array[i].level, parsed_array[i].message);
            }
        }
    }
}

void run_interactive_dashboard(void) {
    signal(SIGINT, handle_dashboard_exit);

    load_initial_logs();

    int ui_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    unlink(DASHBOARD_SOCKET_PATH);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, DASHBOARD_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    bind(ui_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(ui_sock, 5);

    printf("\033[?25l"); 
    printf("\033[2J");   

    while (dashboard_active) {
        printf("\033[H"); 
        print_header();

        double cpu = get_cpu_usage();
        double ram = get_ram_usage();
        double swap = get_swap_usage();
        double temp = get_cpu_temp();

        const char* cpu_color = cpu > 80 ? "\033[31m" : (cpu > 50 ? "\033[33m" : "\033[32m");
        const char* ram_color = ram > 80 ? "\033[31m" : (ram > 50 ? "\033[33m" : "\033[32m");
        const char* swap_color = swap > 50 ? "\033[31m" : (swap > 10 ? "\033[33m" : "\033[32m");

        char temp_str[32] = "";
        if (temp >= 0) {
            snprintf(temp_str, sizeof(temp_str), "(%.1f°C)", temp);
        }

        print_bar("CPU Usage", cpu, cpu_color, temp >= 0 ? temp_str : NULL);
        print_bar("RAM Usage", ram, ram_color, NULL);
        print_bar("Swap Usage", swap, swap_color, NULL);
        print_uptime();
        print_recent_logs();
        printf("\033[J"); 

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(ui_sock, &readfds);
        
        struct timeval tv = {1, 0};
        int activity = select(ui_sock + 1, &readfds, NULL, NULL, &tv);

        if (activity > 0 && FD_ISSET(ui_sock, &readfds)) {
            int client_sock = accept(ui_sock, NULL, NULL);
            if (client_sock >= 0) {
                char buffer[512] = {0};
                int bytes = read(client_sock, buffer, sizeof(buffer) - 1);
                if (bytes > 0) {
                    buffer[bytes] = '\0';
                    char formatted_msg[1024]; 
                    snprintf(formatted_msg, sizeof(formatted_msg), "[LIVE IPC] %s", buffer);
                    strncpy(recent_logs[log_count % MAX_LOGS], formatted_msg, 511);
                    log_count++;
                }
                close(client_sock);
            }
        }
    }

    printf("\033[?25h\n");
    close(ui_sock);
    unlink(DASHBOARD_SOCKET_PATH);
}
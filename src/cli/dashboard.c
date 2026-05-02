#include "cli/dashboard.h"
#include "utils/logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>

// Dashboard Config
#define DASHBOARD_SOCKET_PATH "/tmp/imp_dashboard.sock"
#define UI_SOCKET_BACKLOG     5
#define UI_SELECT_TIMEOUT_SEC 1

// Internal limits
#define MAX_LOGS             500
#define MAX_LOG_LEN          512
#define MAX_LOGS_PER_MODULE  5
#define MAX_TIME_LEN         16
#define MAX_MODULE_NAME_LEN  32
#define MAX_LOG_LEVEL_LEN    16
#define MAX_MESSAGE_LEN      256
#define MAX_LINE_SIZE        256
#define MAX_TEMP_STR_LEN     32
#define MAX_UNIQUE_MODULES   10
#define MAX_DATE_LEN         32
#define MAX_PID_LEN          32
#define MAX_FILE_LEN         64
#define IPC_BUFFER_SIZE      512
#define FORMATTED_MSG_SIZE   1024

// UI Layout Constants
#define BAR_WIDTH            40
#define DASH_PADDING_TOTAL   55
#define SECONDS_IN_HOUR      3600
#define SECONDS_IN_MINUTE    60

// Thresholds for UI colors
#define USAGE_CRITICAL_PCT   80.0
#define USAGE_WARNING_PCT    50.0
#define SWAP_WARNING_PCT     10.0

// Internal defaults
#define DEFAULT_LOG_COUNT    0
#define DEFAULT_TOTAL_RAM    0
#define DEFAULT_AVAILABLE_RAM 0
#define DEFAULT_TOTAL_SWAP   0
#define DEFAULT_AVAILABLE_SWAP 0

// Terminal UI Macros
#define COLOR_RESET       "\033[0m"
#define COLOR_BOLD        "\033[1m"
#define COLOR_DIM         "\033[2m"
#define COLOR_RED         "\033[31m"
#define COLOR_GREEN       "\033[32m"
#define COLOR_YELLOW      "\033[33m"
#define COLOR_BOLD_RED    "\033[1;31m"
#define COLOR_BOLD_YELLOW "\033[1;33m"
#define COLOR_BOLD_BLUE   "\033[1;34m"
#define COLOR_BOLD_CYAN   "\033[1;36m"
#define COLOR_BOLD_WHITE  "\033[1;37m"
#define TERM_CLEAR_LINE   "\033[K"
#define TERM_CLEAR_SCREEN "\033[2J"
#define TERM_CLEAR_DOWN   "\033[J"
#define TERM_CURSOR_HOME  "\033[H"
#define TERM_HIDE_CURSOR  "\033[?25l"
#define TERM_SHOW_CURSOR  "\033[?25h"

volatile sig_atomic_t dashboard_active = true;

static char recent_logs[MAX_LOGS][MAX_LOG_LEN];
static int log_count = DEFAULT_LOG_COUNT;

typedef struct {
    char time[MAX_TIME_LEN];
    char module[MAX_MODULE_NAME_LEN];
    char level[MAX_LOG_LEVEL_LEN];
    char message[MAX_MESSAGE_LEN];
    int is_valid;
} ParsedLog;

void handle_dashboard_exit(int sig) {
    (void)sig;
    dashboard_active = false;
}

static void print_header() {
    printf(COLOR_BOLD_WHITE "I.M.P. v1.0.0" COLOR_RESET TERM_CLEAR_LINE "\n\n");
}

static void print_uptime() {
    FILE *fp = fopen("/proc/uptime", "r");
    double uptime_sec = 0;
    if (fp) {
        fscanf(fp, "%lf", &uptime_sec);
        fclose(fp);
    }
    int h = (int)uptime_sec / SECONDS_IN_HOUR;
    int m = ((int)uptime_sec % SECONDS_IN_HOUR) / SECONDS_IN_MINUTE;
    
    printf("\n" COLOR_BOLD "System Uptime:" COLOR_RESET " %02dh %02dm" TERM_CLEAR_LINE "\n", h, m);
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
    if (fp == NULL) return 0.0;

    char line[MAX_LINE_SIZE];
    long total_ram = DEFAULT_TOTAL_RAM, available_ram = DEFAULT_AVAILABLE_RAM;

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line, "%*s %ld", &total_ram);
        }
        
        if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line, "%*s %ld", &available_ram);
        }
    }
    fclose(fp);

    if (total_ram == DEFAULT_TOTAL_RAM) {
        return 0.0;
    }

    return ((double)(total_ram - available_ram) / total_ram) * 100.0;
}

static double get_swap_usage() {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp == NULL) {
        return 0.0;
    }

    char line[MAX_LINE_SIZE];
    long total_swap = DEFAULT_TOTAL_SWAP, free_swap = DEFAULT_AVAILABLE_SWAP;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "SwapTotal:", 10) == 0) {
            sscanf(line, "%*s %ld", &total_swap);
        }
        
        if (strncmp(line, "SwapFree:", 9) == 0) {
            sscanf(line, "%*s %ld", &free_swap);
        }
    }
    fclose(fp);
    
    if (total_swap == DEFAULT_TOTAL_SWAP) {
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

static void print_bar(const char* label, double percent, const char* bar_color, const char* extra_text) {
    int filled = (int)((percent / 100.0) * BAR_WIDTH);

    printf(COLOR_BOLD "%-10s" COLOR_RESET " [", label);
    printf("%s", bar_color);
    for (int i = 0; i < BAR_WIDTH; i++) {
        if (i < filled) printf("■");
        else printf(" ");
    }

    if (extra_text) {
        printf(COLOR_RESET "] %5.1f%%  " COLOR_YELLOW "%s" COLOR_RESET TERM_CLEAR_LINE "\n", percent, extra_text);
    } else {
        printf(COLOR_RESET "] %5.1f%%" TERM_CLEAR_LINE "\n", percent);
    }
}

static void load_initial_logs() {
    FILE *fp = fopen(LOG_FILE_PATH, "r");
    if (fp == NULL) return;
    
    char buffer[MAX_LOG_LEN];
    while (fgets(buffer, sizeof(buffer), fp)) {
        strncpy(recent_logs[log_count % MAX_LOGS], buffer, MAX_LOG_LEN - 1);
        log_count++;
    }
    fclose(fp);
}

static void parse_log_line(const char* raw_line, ParsedLog* parsed) {
    parsed->is_valid = 0;
    if (strncmp(raw_line, "[LIVE IPC]", 10) == 0) {
        const char* src = strstr(raw_line, "\"source\":\"");
        const char* lvl = strstr(raw_line, "\"level\":\"");
        const char* msg = strstr(raw_line, "\"message\":\"");
        if (src && lvl && msg) {
            sscanf(src + 10, "%31[^\"]", parsed->module);
            sscanf(lvl + 9, "%15[^\"]", parsed->level);
            sscanf(msg + 11, "%255[^\"]", parsed->message);
            strcpy(parsed->time, "LIVE");
            parsed->is_valid = 1;
        }
    } else {
        char date[MAX_DATE_LEN], time[MAX_TIME_LEN], pid[MAX_PID_LEN], mod[MAX_MODULE_NAME_LEN], file[MAX_FILE_LEN], lvl[MAX_LOG_LEVEL_LEN];
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
        printf("  No recent actions." TERM_CLEAR_LINE "\n");
        return;
    }

    int start = log_count > MAX_LOGS ? log_count % MAX_LOGS : 0;
    int total = log_count > MAX_LOGS ? MAX_LOGS : log_count;

    ParsedLog parsed_array[MAX_LOGS];
    for (int i = 0; i < total; i++) {
        parse_log_line(recent_logs[(start + i) % MAX_LOGS], &parsed_array[i]);
    }

    char unique_modules[MAX_UNIQUE_MODULES][MAX_MODULE_NAME_LEN];
    int unique_count = 0;

    for (int i = 0; i < total; i++) {
        if (!parsed_array[i].is_valid) continue;
        int found = 0;
        for (int j = 0; j < unique_count; j++) {
            if (strcmp(unique_modules[j], parsed_array[i].module) == 0) {
                found = 1; break;
            }
        }
        if (!found && unique_count < MAX_UNIQUE_MODULES) {
            strcpy(unique_modules[unique_count++], parsed_array[i].module);
        }
    }

    for (int m = 0; m < unique_count; m++) {
        printf("\n" COLOR_BOLD_CYAN "[ MODULE: %s ]" COLOR_RESET " ", unique_modules[m]);
        int dash_count = DASH_PADDING_TOTAL - strlen(unique_modules[m]);
        for(int d=0; d<dash_count; d++) printf("-");
        printf(TERM_CLEAR_LINE "\n"); 
        
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

                const char* msg_color = COLOR_DIM; 
                if (strcmp(parsed_array[i].level, "CRITICAL") == 0) {
                    msg_color = COLOR_BOLD_RED;
                } else if (strcmp(parsed_array[i].level, "WARNING") == 0) {
                    msg_color = COLOR_BOLD_YELLOW;
                } else if (strcmp(parsed_array[i].level, "NOTICE") == 0) {
                    msg_color = COLOR_BOLD_BLUE;
                } else if (strcmp(parsed_array[i].level, "INFO") == 0) {
                    msg_color = COLOR_RESET;
                }

                printf("  " COLOR_DIM "[%s]" COLOR_RESET " %s[%-8s] %s" COLOR_RESET TERM_CLEAR_LINE "\n", 
                       parsed_array[i].time, msg_color, parsed_array[i].level, parsed_array[i].message);
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
    listen(ui_sock, UI_SOCKET_BACKLOG);

    printf(TERM_HIDE_CURSOR); 
    printf(TERM_CLEAR_SCREEN);   

    while (dashboard_active) {
        printf(TERM_CURSOR_HOME); 
        print_header();

        double cpu = get_cpu_usage();
        double ram = get_ram_usage();
        double swap = get_swap_usage();
        double temp = get_cpu_temp();

        const char* cpu_color = cpu > USAGE_CRITICAL_PCT ? COLOR_RED : (cpu > USAGE_WARNING_PCT ? COLOR_YELLOW : COLOR_GREEN);
        const char* ram_color = ram > USAGE_CRITICAL_PCT ? COLOR_RED : (ram > USAGE_WARNING_PCT ? COLOR_YELLOW : COLOR_GREEN);
        const char* swap_color = swap > USAGE_WARNING_PCT ? COLOR_RED : (swap > SWAP_WARNING_PCT ? COLOR_YELLOW : COLOR_GREEN);

        char temp_str[MAX_TEMP_STR_LEN] = "";
        if (temp >= 0) {
            snprintf(temp_str, sizeof(temp_str), "(%.1f°C)", temp);
        }

        print_bar("CPU Usage", cpu, cpu_color, temp >= 0 ? temp_str : NULL);
        print_bar("RAM Usage", ram, ram_color, NULL);
        print_bar("Swap Usage", swap, swap_color, NULL);
        print_uptime();
        print_recent_logs();
        printf(TERM_CLEAR_DOWN); 

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(ui_sock, &readfds);
        
        struct timeval tv = {UI_SELECT_TIMEOUT_SEC, 0};
        int activity = select(ui_sock + 1, &readfds, NULL, NULL, &tv);

        if (activity > 0 && FD_ISSET(ui_sock, &readfds)) {
            int client_sock = accept(ui_sock, NULL, NULL);
            if (client_sock >= 0) {
                char buffer[IPC_BUFFER_SIZE] = {0};
                int bytes = read(client_sock, buffer, sizeof(buffer) - 1);
                if (bytes > 0) {
                    buffer[bytes] = '\0';
                    char formatted_msg[FORMATTED_MSG_SIZE]; 
                    snprintf(formatted_msg, sizeof(formatted_msg), "[LIVE IPC] %s", buffer);
                    strncpy(recent_logs[log_count % MAX_LOGS], formatted_msg, MAX_LOG_LEN - 1);
                    log_count++;
                }
                close(client_sock);
            }
        }
    }

    printf(TERM_SHOW_CURSOR "\n");
    close(ui_sock);
    unlink(DASHBOARD_SOCKET_PATH);
}
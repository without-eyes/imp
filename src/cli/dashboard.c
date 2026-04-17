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
#define MAX_LOGS 10

volatile sig_atomic_t dashboard_active = 1;

static char recent_logs[MAX_LOGS][256];
static int log_count = 0;

void handle_dashboard_exit(int sig) {
    (void)sig;
    dashboard_active = 0;
}

static double get_ram_usage() {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp == NULL) return 0.0;

    char line[256];
    long total_ram = 0, available_ram = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "MemTotal:", 9) == 0) sscanf(line, "%*s %ld", &total_ram);
        if (strncmp(line, "MemAvailable:", 13) == 0) sscanf(line, "%*s %ld", &available_ram);
    }
    fclose(fp);

    if (total_ram == 0) return 0.0;
    return ((double)(total_ram - available_ram) / total_ram) * 100.0;
}

static double get_cpu_usage() {
    static long prev_idle = 0, prev_total = 0;
    FILE *fp = fopen("/proc/stat", "r");
    if (fp == NULL) return 0.0;

    long user, nice, system, idle, iowait, irq, softirq;
    fscanf(fp, "cpu %ld %ld %ld %ld %ld %ld %ld", &user, &nice, &system, &idle, &iowait, &irq, &softirq);
    fclose(fp);

    long total = user + nice + system + idle + iowait + irq + softirq;
    long total_diff = total - prev_total;
    long idle_diff = idle - prev_idle;

    prev_total = total;
    prev_idle = idle;

    if (total_diff == 0) return 0.0;
    return ((double)(total_diff - idle_diff) / total_diff) * 100.0;
}

static void print_bar(const char* label, double percent, const char* color) {
    int bar_width = 40;
    int filled = (int)((percent / 100.0) * bar_width);
    
    printf("\033[1m%-10s\033[0m [", label);
    printf("%s", color);
    for (int i = 0; i < bar_width; i++) {
        if (i < filled) printf("■");
        else printf(" ");
    }
    printf("\033[0m] %5.1f%%\n", percent);
}

static void load_initial_logs() {
    FILE *fp = fopen(LOG_FILE_PATH, "r");
    if (fp == NULL) return;

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), fp)) {
        strncpy(recent_logs[log_count % MAX_LOGS], buffer, 255);
        log_count++;
    }
    fclose(fp);
}

static void print_recent_logs() {
    if (log_count == 0) {
        printf("  No recent actions.\n");
        return;
    }

    int start = log_count > MAX_LOGS ? log_count % MAX_LOGS : 0;
    int total = log_count > MAX_LOGS ? MAX_LOGS : log_count;

    for (int i = 0; i < total; i++) {
        char* line = recent_logs[(start + i) % MAX_LOGS];

        if (strstr(line, "CRITICAL")) printf("\033[1;31m%s\033[0m", line);
        else if (strstr(line, "WARNING")) printf("\033[1;33m%s\033[0m", line);
        else if (strstr(line, "NOTICE")) printf("\033[1;36m%s\033[0m", line);
        else printf("\033[2m%s\033[0m", line);

        if (line[strlen(line) - 1] != '\n') printf("\n");
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

    while (dashboard_active) {
        printf("\033[2J\033[H");

        printf("\033[1;34m======================================================================\033[0m\n");
        printf("\033[1;37m                I.M.P. INTERACTIVE DASHBOARD v1.0.0                   \033[0m\n");
        printf("\033[1;34m======================================================================\033[0m\n\n");

        double cpu = get_cpu_usage();
        double ram = get_ram_usage();

        const char* cpu_color = cpu > 80 ? "\033[31m" : (cpu > 50 ? "\033[33m" : "\033[32m");
        const char* ram_color = ram > 80 ? "\033[31m" : (ram > 50 ? "\033[33m" : "\033[32m");

        print_bar("CPU Usage", cpu, cpu_color);
        print_bar("RAM Usage", ram, ram_color);

        printf("\n\033[1;34m-------------------------- RECENT ACTIONS ----------------------------\033[0m\n");
        print_recent_logs();

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(ui_sock, &readfds);

        struct timeval tv = {1, 0};

        int activity = select(ui_sock + 1, &readfds, NULL, NULL, &tv);
        
        if (activity > 0 && FD_ISSET(ui_sock, &readfds)) {
            int client_sock = accept(ui_sock, NULL, NULL);
            if (client_sock >= 0) {
                char buffer[256] = {0};
                int bytes = read(client_sock, buffer, sizeof(buffer) - 1);
                if (bytes > 0) {
                    buffer[bytes] = '\0';
                    
                    char formatted_msg[512];
                    snprintf(formatted_msg, sizeof(formatted_msg), "[LIVE IPC] %s", buffer);
                    strncpy(recent_logs[log_count % MAX_LOGS], formatted_msg, 255);
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
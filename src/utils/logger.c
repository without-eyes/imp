#include "../../include/utils/logger.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <time.h>

typedef struct {
    FILE* fd;
    bool isInitialized;
    log_level_t level;
} LoggerConfig;

static LoggerConfig* log_get_config(void) {
    static LoggerConfig config = {
        .fd = NULL,
        .isInitialized = false,
        .level = LOG_LEVEL_UNINITIALIZED
    };
    return &config;
}

int log_init(log_level_t level) {
    LoggerConfig* config = log_get_config();

    if (config->isInitialized) {
        return 0;
    }

    config->fd = fopen(LOG_FILE_PATH, "a");
    if (config->fd == NULL) {
        return -1;
    }

    config->isInitialized = true;
    config->level = level;

    return 0;
}

int log_deinit(void) {
    LoggerConfig* config = log_get_config();
    config->isInitialized = false;
    config->level = LOG_LEVEL_UNINITIALIZED;

    if (config->fd != NULL) {
        fclose(config->fd);
        config->fd = NULL;
    }

    return 0;
}

static const char* log_level_to_string(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_CRITICAL: return "CRITICAL";
        case LOG_LEVEL_ERROR:    return "ERROR";
        case LOG_LEVEL_WARNING:  return "WARNING";
        case LOG_LEVEL_NOTICE:   return "NOTICE";
        case LOG_LEVEL_INFO:     return "INFO";
        case LOG_LEVEL_DEBUG:    return "DEBUG";
        default:                 return "UNKNOWN";
    }
}

static void log_get_curr_time(char* buffer, size_t size) {
    time_t currentTime = time(NULL);
    struct tm *sTm = localtime(&currentTime);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", sTm);
}

int log_write(log_level_t level, const char* moduleName, const char* file, int line, const char* format, ...) {
    LoggerConfig* config = log_get_config();
    if (config->isInitialized == false || config->level < level || config->fd == NULL) {
        return 0;
    }

    char buffer[LOG_BUFF_SIZE];
    char time_str[24];
    log_get_curr_time(time_str, sizeof(time_str));

    int prefix_len = snprintf(buffer, LOG_BUFF_SIZE, "[%s][%s][%s:%d][%s] ", time_str, moduleName, file, line, log_level_to_string(level));

    if (prefix_len < 0 || prefix_len >= LOG_BUFF_SIZE) {
        return -1;
    }

    va_list args;
    va_start(args, format);
    
    size_t msg_space = LOG_BUFF_SIZE - prefix_len - 2;
    int msg_len = vsnprintf(buffer + prefix_len, msg_space, format, args);
    
    va_end(args);

    int total_len = prefix_len + msg_len;
    if (total_len > LOG_BUFF_SIZE - 2) {
        total_len = LOG_BUFF_SIZE - 2;
    }
    
    buffer[total_len] = '\n';
    buffer[total_len + 1] = '\0';

    fputs(buffer, config->fd);
    fflush(config->fd);
    
    return 0;
}
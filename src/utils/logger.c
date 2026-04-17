#include "utils/logger.h"

#include <stdio.h>
#include <stdbool.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>

#define TIME_STR_SIZE 32

typedef struct {
    FILE* fd;
    bool isInitialized;
    log_level_t level;
    pthread_mutex_t lock;
} LoggerConfig;

static LoggerConfig* log_get_config(void) {
    static LoggerConfig config = {
        .fd = NULL,
        .isInitialized = false,
        .level = LOG_LEVEL_UNINITIALIZED,
        .lock = PTHREAD_MUTEX_INITIALIZER
    };
    return &config;
}

int log_init(const char* logFilePath, log_level_t level) {
    LoggerConfig* config = log_get_config();

    pthread_mutex_lock(&config->lock);

    if (config->isInitialized) {
        pthread_mutex_unlock(&config->lock);
        return 0;
    }

    config->fd = fopen(logFilePath, "a");
    if (config->fd == NULL) {
        pthread_mutex_unlock(&config->lock);
        return -1;
    }

    config->isInitialized = true;
    config->level = level;

    setvbuf(config->fd, NULL, _IOLBF, 0);

    pthread_mutex_unlock(&config->lock);
    return 0;
}

int log_deinit(void) {
    LoggerConfig* config = log_get_config();
    
    pthread_mutex_lock(&config->lock);

    config->isInitialized = false;
    config->level = LOG_LEVEL_UNINITIALIZED;

    if (config->fd != NULL) {
        fclose(config->fd);
        config->fd = NULL;
    }

    pthread_mutex_unlock(&config->lock);
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
    struct timeval tv;
    gettimeofday(&tv, NULL);

    struct tm sTm;
    localtime_r(&tv.tv_sec, &sTm);

    char time_str[TIME_STR_SIZE];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &sTm);

    snprintf(buffer, size, "%s.%03ld", time_str, tv.tv_usec / 1000);
}

int log_write(log_level_t level, const char* moduleName, const char* file, int line, const char* format, ...) {
    LoggerConfig* config = log_get_config();
    pthread_mutex_lock(&config->lock);

    if (config->isInitialized == false || config->level < level || config->fd == NULL) {
        pthread_mutex_unlock(&config->lock); 
        return 0;
    }

    char buffer[LOG_BUFF_SIZE];
    char time_str[TIME_STR_SIZE];
    log_get_curr_time(time_str, sizeof(time_str));

    int prefix_len = snprintf(buffer, LOG_BUFF_SIZE, "[%s][PID:%d][%s][%s:%d][%s] ", time_str, getpid(), moduleName, file, line, log_level_to_string(level));

    if (prefix_len < 0 || prefix_len >= LOG_BUFF_SIZE) {
        pthread_mutex_unlock(&config->lock);
        return -1;
    }

    va_list args;
    va_start(args, format);
    
    size_t msg_space = LOG_BUFF_SIZE - prefix_len - 2;
    int msg_len = vsnprintf(buffer + prefix_len, msg_space, format, args);
    
    va_end(args);
    if (msg_len < 0) {
        msg_len = 0;
    }

    int total_len = prefix_len + msg_len;
    if (total_len > LOG_BUFF_SIZE - 2) {
        total_len = LOG_BUFF_SIZE - 2;
    }
    
    buffer[total_len] = '\n';
    buffer[total_len + 1] = '\0';

    fputs(buffer, config->fd);
    
    pthread_mutex_unlock(&config->lock);
    return 0;
}
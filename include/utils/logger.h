#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>

#define LOG_FILE_PATH "/var/log/imp/imp.log"
#define LOG_BUFF_SIZE 1024

#define LOG_CRITICAL(moduleName, format, ...) log_write(LOG_LEVEL_CRITICAL, moduleName, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_ERR(moduleName, format, ...) log_write(LOG_LEVEL_ERROR, moduleName, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_WARN(moduleName, format, ...) log_write(LOG_LEVEL_WARNING, moduleName, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_NOTICE(moduleName, format, ...) log_write(LOG_LEVEL_NOTICE, moduleName, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_INFO(moduleName, format, ...) log_write(LOG_LEVEL_INFO, moduleName, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_DEBUG(moduleName, format, ...) log_write(LOG_LEVEL_DEBUG, moduleName, __FILE__, __LINE__, format, ##__VA_ARGS__)

typedef enum {
    LOG_LEVEL_UNINITIALIZED,
    LOG_LEVEL_CRITICAL,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_NOTICE,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG
} log_level_t;

typedef struct {
    bool isInitialized;
    log_level_t level;
} LoggerConfig;


int log_init(log_level_t level);
int log_deinit(void);

__attribute__((format(printf, 5, 6)))
int log_write(log_level_t level, const char* moduleName, const char* file, int line, const char* format, ...);

#endif // LOGGER_H
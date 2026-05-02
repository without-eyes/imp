#ifndef LOGGER_H
#define LOGGER_H

#ifndef LOG_FILE_PATH
#define LOG_FILE_PATH "/var/log/imp/imp.log"
#endif

#define LOG_CRITICAL(module_name, format, ...) log_write(LOG_LEVEL_CRITICAL, module_name, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_ERR(module_name, format, ...) log_write(LOG_LEVEL_ERROR, module_name, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_WARN(module_name, format, ...) log_write(LOG_LEVEL_WARNING, module_name, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_NOTICE(module_name, format, ...) log_write(LOG_LEVEL_NOTICE, module_name, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_INFO(module_name, format, ...) log_write(LOG_LEVEL_INFO, module_name, __FILE__, __LINE__, format, ##__VA_ARGS__)
#define LOG_DEBUG(module_name, format, ...) log_write(LOG_LEVEL_DEBUG, module_name, __FILE__, __LINE__, format, ##__VA_ARGS__)

typedef enum {
    LOG_LEVEL_UNINITIALIZED,
    LOG_LEVEL_CRITICAL,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_NOTICE,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG
} log_level_t;

int log_init(const char* log_file_path, log_level_t level);
int log_deinit(void);

__attribute__((format(printf, 5, 6)))
int log_write(log_level_t level, const char* module_name, const char* file, int line, const char* format, ...);

#endif // LOGGER_H
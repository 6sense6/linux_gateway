#ifndef LOGGER_H
#define LOGGER_H

/* 日志级别，数值大小就是严重程度，供过滤比较用 */
typedef enum {
    LOG_LVL_DEBUG = 0,
    LOG_LVL_INFO,
    LOG_LVL_WARN,
    LOG_LVL_ERROR,
} log_level_t;

/* filepath 传 NULL 则输出到 stderr；成功返回 0，失败返回 -1 */
int logger_init(const char *filepath, log_level_t min_level);

void logger_close(void);

void logger_log(log_level_t level, const char *file, int line,
                const char *fmt, ...);

/* 用户侧只调这四个宏，不直接调 logger_log */
#define LOG_DEBUG(fmt, ...) logger_log(LOG_LVL_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  logger_log(LOG_LVL_INFO,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  logger_log(LOG_LVL_WARN,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) logger_log(LOG_LVL_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif
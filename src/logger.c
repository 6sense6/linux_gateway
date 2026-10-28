#include <stdio.h>      /* vsnprintf、perror */
#include <stdarg.h>     /* va_list 三件套 */
#include <time.h>       /* time、localtime_r、strftime */
#include <fcntl.h>      /* open 和 O_ 开头的标志宏（P48） */
#include <unistd.h>     /* write、close（P52） */
#include <pthread.h>    /* 互斥锁（Day2） */
#include "logger.h"

#define LOG_BUF_SIZE 512

/* 日志输出目标，默认 2 号描述符即 stderr（笔记 1.9 节） */
static int g_log_fd = 2;
static log_level_t g_min_level = LOG_LVL_DEBUG;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* 枚举值当数组下标用，顺序必须和 log_level_t 一一对应 */
static const char *g_level_str[] = { "DEBUG", "INFO", "WARN", "ERROR" };

int logger_init(const char *filepath, log_level_t min_level)
{
    /* 防止重复 init 泄漏上一次打开的 fd */
    if (g_log_fd >= 3)
        close(g_log_fd);

    if (filepath != NULL) {
        int fd = open(filepath, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd == -1) {
            perror("logger_init open");
            return -1;
        }
        g_log_fd = fd;
    } else {
        g_log_fd = 2;  /* stderr */
    }

    g_min_level = min_level;
    return 0;
}

void logger_close(void)
{
    /* 0/1/2 是进程自带的，只有自己 open 出来的（>=3）才需要关 */
    if (g_log_fd >= 3)
        close(g_log_fd);
    g_log_fd = 2;
}

void logger_log(log_level_t level, const char *file, int line,
                const char *fmt, ...)
{
    char timebuf[32];
    char msgbuf[LOG_BUF_SIZE];
    char linebuf[LOG_BUF_SIZE + 64];
    va_list ap;
    time_t now;
    struct tm tm_now;
    int msglen, linelen;
    ssize_t n;

    /* 级别不够直接返回，连时间都不取，零成本丢弃 */
    if (level < g_min_level)
        return;

    /* 第一段：时间戳 */
    time(&now);
    localtime_r(&now, &tm_now);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_now);

    /* 第二段：用户内容（把 ... 参数格式化成字符串） */
    va_start(ap, fmt);
    msglen = vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
    va_end(ap);
    if (msglen < 0)
        return;

    /* 第三段：拼成完整一行 */
    linelen = snprintf(linebuf, sizeof(linebuf), "%s [%-5s] %s:%d %s\n",
                       timebuf, g_level_str[level], file, line, msgbuf);
    if (linelen < 0)
        return;
    if (linelen >= (int)sizeof(linebuf))
        linelen = sizeof(linebuf) - 1;  /* 被截断时只写缓冲区里实际有的部分 */

    /* 第四段：一次性写出，锁保证多线程不串行 */
    pthread_mutex_lock(&g_log_mutex);
    n = write(g_log_fd, linebuf, linelen);
    (void)n;  /* 日志写失败不能影响业务，这里只能吞掉 */
    pthread_mutex_unlock(&g_log_mutex);
}
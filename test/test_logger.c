#include <stdio.h>
#include <string.h>
#include "logger.h"

static int g_pass = 0;
static int g_fail = 0;

/* 判定并计数, 测试结论必须 printf 出来, 不靠肉眼数 */
static void check(const char *name, int cond)
{
    if (cond) {
        g_pass++;
        printf("[PASS] %s\n", name);
    } else {
        g_fail++;
        printf("[FAIL] %s\n", name);
    }
}

int main(void)
{
    const char *path = "build/run.log";
    FILE *fp;
    char line[256];
    int cnt_debug = 0, cnt_info = 0, cnt_warn = 0, cnt_error = 0;

    /* logger 用 O_APPEND 打开, 每次测试前删掉, 从干净文件开始 */
    remove(path);

    /* 最小级别设 INFO, 让 DEBUG 真正被过滤 */
    if (logger_init(path, LOG_LVL_INFO) != 0) {
        printf("[FAIL] logger_init 失败\n");
        return 1;
    }

    LOG_DEBUG("这条 DEBUG 应被过滤, 不落盘");
    LOG_INFO("这条 INFO 应落盘");
    LOG_WARN("这条 WARN 应落盘");
    LOG_ERROR("这条 ERROR 应落盘");

    logger_close();

    fp = fopen(path, "r");
    if (fp == NULL) {
        printf("[FAIL] 打开日志文件 %s 失败\n", path);
        return 1;
    }
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "[DEBUG")) cnt_debug++;
        if (strstr(line, "[INFO"))  cnt_info++;
        if (strstr(line, "[WARN"))  cnt_warn++;
        if (strstr(line, "[ERROR")) cnt_error++;
    }
    fclose(fp);

    check("DEBUG 被过滤(0条)", cnt_debug == 0);
    check("INFO 落盘(1条)",    cnt_info == 1);
    check("WARN 落盘(1条)",    cnt_warn == 1);
    check("ERROR 落盘(1条)",   cnt_error == 1);

    printf("total: pass %d, fail %d\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

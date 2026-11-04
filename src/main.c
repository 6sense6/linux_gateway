#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

#include "logger.h"
#include "serial.h"
#include "ringbuffer.h"
#include "frame.h"
#include "tcp_client.h"

#define SERIAL_DEV   "/tmp/ttyGateway"
#define SERVER_IP    "127.0.0.1"
#define SERVER_PORT  9000
#define DEVICE_ID    1

#define RB_UART_SIZE 4096
#define RB_JSON_SIZE 4096
#define JSON_LINE_MAX 128

/* 三线程共享上下文：约定每个线程只碰自己那一份 fd + 缓冲，不交叉 */
typedef struct {
    int          serial_fd;
    int          sock_fd;
    ringbuffer_t rb_uart;   /* 原始字节流：串口 → 解析 */
    ringbuffer_t rb_json;   /* JSON 行(带\n)：解析 → 上传 */
} gateway_ctx_t;

/* ── 线程1：采集 ── */
static void *collect_thread(void *arg)
{
    gateway_ctx_t *ctx = (gateway_ctx_t *)arg;
    uint8_t buf[256];
    int n;

    while (1) {
        n = serial_read(ctx->serial_fd, buf, sizeof(buf));
        if (n < 0) {
            LOG_ERROR("collect: serial broken, shutdown pipeline");
            ringbuffer_stop(&ctx->rb_uart);
            ringbuffer_stop(&ctx->rb_json);
            break;
        }
        if (n == 0) {                       /* VTIME 200ms 超时心跳，不是错误 */
            LOG_DEBUG("collect: no data (timeout)");
            continue;
        }
        for (int i = 0; i < n; i++) {
            if (ringbuffer_write(&ctx->rb_uart, buf[i]) != 0) {
                LOG_INFO("collect: rb_uart stopped, exit");
                return NULL;
            }
        }
    }
    return NULL;
}

/* ── 线程2：解析 + 拼 JSON ── */
static void *parse_thread(void *arg)
{
    gateway_ctx_t *ctx = (gateway_ctx_t *)arg;
    frame_parser_t parser;                  /* 本线程私有，不进共享上下文 */
    frame_t f;
    uint8_t b;
    unsigned long seq = 0;

    frame_parser_init(&parser);

    while (1) {
        if (ringbuffer_read(&ctx->rb_uart, &b) != 0) {
            LOG_INFO("parse: rb_uart stopped, exit");
            break;
        }
        if (frame_parse_byte(&parser, b, &f) != 1)
            continue;                        /* 继续收，或坏帧已丢弃 */

        if (f.len != 4) {
            LOG_WARN("parse: frame len=%d != 4, skip", f.len);
            continue;
        }
        int16_t temp = (int16_t)((f.payload[0] << 8) | f.payload[1]);
        int16_t humi = (int16_t)((f.payload[2] << 8) | f.payload[3]);

        char line[JSON_LINE_MAX];
        int n = snprintf(line, sizeof(line),
                         "{\"device_id\":%d,\"seq\":%lu,\"temp\":%.1f,\"humi\":%.1f}\n",
                         DEVICE_ID, seq++, temp / 10.0, humi / 10.0);
        if (n < 0 || n >= (int)sizeof(line)) {
            LOG_WARN("parse: json overflow (n=%d), drop", n);
            continue;
        }
        for (int i = 0; i < n; i++) {        /* 连 '\n' 一起写，作为消息边界 */
            if (ringbuffer_write(&ctx->rb_json, (uint8_t)line[i]) != 0) {
                LOG_INFO("parse: rb_json stopped, exit");
                return NULL;
            }
        }
        LOG_DEBUG("parse: frame seq=%lu encoded", seq - 1);
    }
    return NULL;
}

/* ── 线程3：按行取 + 上传 ── */
static void *upload_thread(void *arg)
{
    gateway_ctx_t *ctx = (gateway_ctx_t *)arg;
    char line[JSON_LINE_MAX];
    int idx = 0;
    uint8_t b;

    while (1) {
        if (ringbuffer_read(&ctx->rb_json, &b) != 0) {
            LOG_INFO("upload: rb_json stopped, exit");
            break;
        }

        /* 越界防护：本行超长则丢到下一换行，重新对齐，绝不数组越界 */
        if (idx >= (int)sizeof(line) - 1) {
            LOG_WARN("upload: line too long, resync at newline");
            idx = 0;
            while (b != '\n') {
                if (ringbuffer_read(&ctx->rb_json, &b) != 0)
                    return NULL;             /* 排空过程中队列被 stop */
            }
            continue;
        }

        line[idx++] = (char)b;
        if (b == '\n') {
            int n = tcp_client_send(ctx->sock_fd, line, idx);
            if (n < 0) {
                /* 具体原因 tcp_client_send 已打 ERROR，这里只记丢帧 */
                LOG_ERROR("upload: send failed, frame dropped");
            } else {
                LOG_DEBUG("upload: sent %d bytes", n);
            }
            idx = 0;
        }
    }
    return NULL;
}

int main(void)
{
    gateway_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.serial_fd = -1;
    ctx.sock_fd   = -1;

    pthread_t tids[3];
    int n_threads = 0;
    int rb_uart_ok = 0, rb_json_ok = 0;
    int rc = 0;

    if (logger_init(NULL, LOG_LVL_DEBUG) != 0)
        return 1;

    ctx.serial_fd = serial_open(SERIAL_DEV);
    if (ctx.serial_fd < 0) {
        LOG_ERROR("gateway: serial_open failed");
        rc = 1;
        goto cleanup;
    }

    ctx.sock_fd = tcp_client_connect(SERVER_IP, SERVER_PORT);
    if (ctx.sock_fd < 0) {
        LOG_ERROR("gateway: connect failed (reconnect on Day6)");
        rc = 1;
        goto cleanup;
    }

    if (ringbuffer_init(&ctx.rb_uart, RB_UART_SIZE) != 0) {
        LOG_ERROR("gateway: rb_uart init failed");
        rc = 1;
        goto cleanup;
    }
    rb_uart_ok = 1;

    if (ringbuffer_init(&ctx.rb_json, RB_JSON_SIZE) != 0) {
        LOG_ERROR("gateway: rb_json init failed");
        rc = 1;
        goto cleanup;
    }
    rb_json_ok = 1;

    /* 逐个检查 pthread_create 返回值；失败时 stop 唤醒已建的线程再 join */
    if (pthread_create(&tids[0], NULL, collect_thread, &ctx) != 0) {
        LOG_ERROR("gateway: create collect thread failed");
        rc = 1; goto cleanup;
    }
    n_threads = 1;
    if (pthread_create(&tids[1], NULL, parse_thread, &ctx) != 0) {
        LOG_ERROR("gateway: create parse thread failed");
        rc = 1; goto cleanup;
    }
    n_threads = 2;
    if (pthread_create(&tids[2], NULL, upload_thread, &ctx) != 0) {
        LOG_ERROR("gateway: create upload thread failed");
        rc = 1; goto cleanup;
    }
    n_threads = 3;

    LOG_INFO("gateway: 3 threads started, waiting ...");

    /* Day5 正常路径：主线程 join 等线程退出。
       退出触发点 = 采集线程串口损坏后 stop 两条队列；Ctrl+C 的优雅退出是 Day6 内容 */
    for (int i = 0; i < 3; i++)
        pthread_join(tids[i], NULL);
    n_threads = 0;    /* 已全部 join，cleanup 不再重复 join */
    LOG_INFO("gateway: all threads exited");

cleanup:
    if (rb_uart_ok) ringbuffer_stop(&ctx.rb_uart);
    if (rb_json_ok) ringbuffer_stop(&ctx.rb_json);
    for (int i = 0; i < n_threads; i++)
        pthread_join(tids[i], NULL);
    if (rb_uart_ok) ringbuffer_destroy(&ctx.rb_uart);
    if (rb_json_ok) ringbuffer_destroy(&ctx.rb_json);
    if (ctx.serial_fd >= 0) serial_close(ctx.serial_fd);
    if (ctx.sock_fd >= 0)   tcp_client_close(ctx.sock_fd);
    logger_close();
    return rc;
}
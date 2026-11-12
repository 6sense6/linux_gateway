#include "logger.h"
#include "serial.h"
#include "frame.h"
#include "ringbuffer.h"
#include "tcp_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#define SERIAL_DEV     "/tmp/ttyGateway"
#define SERVER_IP      "127.0.0.1"
#define SERVER_PORT    9000
#define DEVICE_ID      1
#define JSON_LINE_MAX  128
#define RB_UART_SIZE   4096
#define RB_JSON_SIZE   4096
#define RECONNECT_MS   3000

/* 全局运行标志：信号处理器置 0，各线程轮询感知退出。
   用 atomic_int 而非 volatile sig_atomic_t：C11 下 volatile 跨线程访问是 data race (UB)；
   atomic 配合 relaxed 只保证原子读写不保证顺序——标志位不需要顺序，够用。
   atomic_int 在 x86/ARM 上 lock-free，信号处理器里安全。 */
static atomic_int g_running = 1;

typedef struct {
    int          serial_fd;
    int          sock_fd;
    _Atomic int  exit_code;   /* 线程运行时故障置 1；main 据此返回非 0 */
    ringbuffer_t rb_uart;     /* 串口原始字节 → 解析 */
    ringbuffer_t rb_json;     /* JSON 行(带 \n) → 上传 */
} gateway_ctx_t;

/* ---------------- 信号 ---------------- */
static void on_signal(int sig)
{
    (void)sig;
    atomic_store_explicit(&g_running, 0, memory_order_relaxed);
}

static void setup_signals(void)
{
    struct sigaction sa;

    /* 必修①：SIGPIPE 全局忽略，send 到已关闭连接只返回 EPIPE，不再杀进程 */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPIPE, &sa, NULL);

    /* 必修③：SIGINT/SIGTERM → 置 g_running=0，走优雅退出 */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

/* ---------------- 采集线程 ---------------- */
static void *collect_thread(void *arg)
{
    gateway_ctx_t *ctx = (gateway_ctx_t *)arg;
    uint8_t buf[256];
    int n;

    while (atomic_load_explicit(&g_running, memory_order_relaxed)) {
        n = serial_read(ctx->serial_fd, buf, sizeof(buf));
        if (n < 0) {                              /* 串口损坏，致命 */
            LOG_ERROR("collect: serial broken, shutdown");
            atomic_store(&ctx->exit_code, 1);
            break;
        }
        if (n == 0) {                             /* VTIME 200ms 超时心跳 */
            if (ringbuffer_is_stopped(&ctx->rb_uart))
                break;
            LOG_DEBUG("collect: no data (timeout)");
            continue;
        }
        int wfail = 0;
        for (int i = 0; i < n; i++) {
            if (ringbuffer_write(&ctx->rb_uart, buf[i]) != 0) {
                wfail = 1;
                break;
            }
        }
        if (wfail)
            break;
    }

    /* 无论何种原因退出，都 stop 两条 rb 做级联关闭 */
    LOG_INFO("collect: exit, stopping pipeline");
    ringbuffer_stop(&ctx->rb_uart);
    ringbuffer_stop(&ctx->rb_json);
    return NULL;
}

/* ---------------- 解析线程（与 Day5 一致） ---------------- */
static void *parse_thread(void *arg)
{
    gateway_ctx_t *ctx = (gateway_ctx_t *)arg;
    frame_parser_t parser;
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
            continue;

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
        for (int i = 0; i < n; i++) {
            if (ringbuffer_write(&ctx->rb_json, (uint8_t)line[i]) != 0) {
                LOG_INFO("parse: rb_json stopped, exit");
                return NULL;
            }
        }
        LOG_DEBUG("parse: frame seq=%lu encoded", seq - 1);
    }
    return NULL;
}

/* ---------------- 上传线程 + 重连 ---------------- */
/* 返回 1=已重连；0=正在退出（g_running=0 或管线已停） */
static int upload_reconnect(gateway_ctx_t *ctx)
{
    while (atomic_load_explicit(&g_running, memory_order_relaxed)) {
        if (ringbuffer_is_stopped(&ctx->rb_json))
            return 0;
        ctx->sock_fd = tcp_client_connect(SERVER_IP, SERVER_PORT);
        if (ctx->sock_fd >= 0) {
            LOG_INFO("upload: reconnected");
            return 1;
        }
        LOG_WARN("upload: reconnect failed, retry in %d ms", RECONNECT_MS);
        for (int i = 0; i < RECONNECT_MS / 100 &&
             atomic_load_explicit(&g_running, memory_order_relaxed); i++)
            usleep(100000);                       /* 分片 sleep，Ctrl+C 最多 100ms 响应 */
    }
    return 0;
}

static void *upload_thread(void *arg)
{
    gateway_ctx_t *ctx = (gateway_ctx_t *)arg;
    char line[JSON_LINE_MAX];
    int idx = 0;
    uint8_t b;

    while (atomic_load_explicit(&g_running, memory_order_relaxed)) {
        if (ringbuffer_read(&ctx->rb_json, &b) != 0) {
            LOG_INFO("upload: rb_json stopped, exit");
            break;
        }

        /* 超长行重同步（正常数据流不可达，防御性） */
        if (idx >= (int)sizeof(line) - 1) {
            LOG_WARN("upload: line too long, resync");
            idx = 0;
            while (b != '\n') {
                if (ringbuffer_read(&ctx->rb_json, &b) != 0)
                    goto done;                    /* 排空中队列被 stop */
            }
            continue;
        }

        line[idx++] = (char)b;
        if (b != '\n')
            continue;

        /* 发送整行；失败则重连并重发同一行 */
        while (atomic_load_explicit(&g_running, memory_order_relaxed)) {
            if (ctx->sock_fd >= 0 && tcp_client_peer_closed(ctx->sock_fd)) {
                LOG_WARN("upload: peer closed (FIN), reconnect");
                tcp_client_close(ctx->sock_fd);
                ctx->sock_fd = -1;
            }
            if (ctx->sock_fd < 0 && !upload_reconnect(ctx))
                goto done;                        /* 正在退出 */

            int n = tcp_client_send(ctx->sock_fd, line, idx);
            if (n >= 0)
                break;                            /* 发成功 */
            LOG_WARN("upload: send failed, reconnect");
            tcp_client_close(ctx->sock_fd);
            ctx->sock_fd = -1;
        }
        idx = 0;
    }

done:
    /* 必修：upload 退出时必须 stop 管线。
       否则断线 + 缓冲区满后 Ctrl+C 会死锁：
       collect/parse 卡在 ringbuffer_write 的 cond_wait，
       只有这里的广播能唤醒它们。collect 也会 stop（幂等无害）。 */
    LOG_INFO("upload: exit, stopping pipeline");
    ringbuffer_stop(&ctx->rb_uart);
    ringbuffer_stop(&ctx->rb_json);
    return NULL;
}

/* ---------------- usage ---------------- */
static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s [serial_dev] [server_ip] [server_port]\n"
        "  serial_dev   default %s\n"
        "  server_ip    default %s\n"
        "  server_port  default %d\n",
        prog, SERIAL_DEV, SERVER_IP, SERVER_PORT);
}

/* ---------------- main ---------------- */
int main(int argc, char *argv[])
{
    const char *serial_dev = SERIAL_DEV;
    const char *server_ip  = SERVER_IP;
    int         server_port = SERVER_PORT;

    gateway_ctx_t ctx;
    pthread_t tids[3];
    int n_threads = 0;
    int rb_uart_ok = 0, rb_json_ok = 0;
    int rc = 0;

    if (argc > 1) { serial_dev  = argv[1]; }
    if (argc > 2) { server_ip   = argv[2]; }
    if (argc > 3) { server_port = atoi(argv[3]);
                    if (server_port <= 0 || server_port > 65535) {
                        fprintf(stderr, "invalid port: %s\n", argv[3]);
                        usage(argv[0]);
                        return 1;
                    }}
    if (argc > 4) { usage(argv[0]); return 1; }

    memset(&ctx, 0, sizeof(ctx));
    ctx.serial_fd = -1;
    ctx.sock_fd   = -1;
    atomic_init(&ctx.exit_code, 0);

    setup_signals();

    if (logger_init(NULL, LOG_LVL_DEBUG) != 0)
        return 1;

    ctx.serial_fd = serial_open(serial_dev);
    if (ctx.serial_fd < 0) {
        LOG_ERROR("gateway: serial_open(%s) failed", serial_dev);
        rc = 1;
        goto cleanup;
    }

    ctx.sock_fd = tcp_client_connect(server_ip, server_port);
    if (ctx.sock_fd < 0) {
        LOG_ERROR("gateway: initial connect failed");
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

    if (pthread_create(&tids[0], NULL, collect_thread, &ctx) != 0) {
        LOG_ERROR("gateway: create collect failed");
        rc = 1; goto cleanup;
    }
    n_threads = 1;
    if (pthread_create(&tids[1], NULL, parse_thread, &ctx) != 0) {
        LOG_ERROR("gateway: create parse failed");
        rc = 1; goto cleanup;
    }
    n_threads = 2;
    if (pthread_create(&tids[2], NULL, upload_thread, &ctx) != 0) {
        LOG_ERROR("gateway: create upload failed");
        rc = 1; goto cleanup;
    }
    n_threads = 3;

    LOG_INFO("gateway: 3 threads started, Ctrl+C to stop");

    for (int i = 0; i < 3; i++)
        pthread_join(tids[i], NULL);
    n_threads = 0;
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

    /* 运行时故障→1；优雅退出→rc(0) */
    return atomic_load(&ctx.exit_code) != 0 ? 1 : rc;
}
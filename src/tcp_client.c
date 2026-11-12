#include "tcp_client.h"
#include "logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

int tcp_client_connect(const char *ip, int port)
{
    int fd;
    struct sockaddr_in addr;
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };

    if (ip == NULL || port <= 0 || port > 65535) {
        LOG_ERROR("tcp: invalid ip/port argument");
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR("tcp: socket failed: %s", strerror(errno));
        return -1;
    }

    /* connect 超时：不可达地址内核 SYN 重试 127s，设为 2s 让重连循环有意义。
       副作用：SO_SNDTIMEO 也会作用于后续 send()——tcp_client_send 里
       EAGAIN 视为失败触发重连，语义上可接受（发送缓冲区满 2s 说明对端异常）。 */
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        LOG_WARN("tcp: setsockopt SO_SNDTIMEO failed: %s", strerror(errno));
        /* 不致命，继续；connect 会走内核默认超时 */
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);

    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        LOG_ERROR("tcp: '%s' is not a valid IPv4 address", ip);
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("tcp: connect %s:%d failed: %s", ip, port, strerror(errno));
        close(fd);
        return -1;
    }

    LOG_INFO("tcp: connected to %s:%d, fd=%d", ip, port, fd);
    return fd;
}

int tcp_client_send(int sockfd, const void *buf, int len)
{
    const char *p = (const char *)buf;
    int total = 0;

    if (sockfd < 0 || buf == NULL || len < 0) {
        LOG_ERROR("tcp: send invalid argument");
        return -1;
    }

    while (total < len) {
        ssize_t n = send(sockfd, p + total, (size_t)(len - total), MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            /* EAGAIN/EWOULDBLOCK：SO_SNDTIMEO 超时，或非阻塞 socket 发送缓冲满。
               均视为对端异常，触发重连。 */
            LOG_ERROR("tcp: send failed: %s", strerror(errno));
            return -1;
        }
        if (n == 0) {
            LOG_ERROR("tcp: send returned 0, peer closed connection");
            return -1;
        }
        total += (int)n;
    }
    return total;
}

int tcp_client_close(int sockfd)
{
    int rc = 0;
    if (sockfd >= 0) {
        rc = close(sockfd);
        if (rc != 0)
            LOG_WARN("tcp: close failed: %s", strerror(errno));
    }
    return rc;
}

int tcp_client_peer_closed(int sockfd)
{
    char c;
    ssize_t n;

    if (sockfd < 0)
        return 1;

    n = recv(sockfd, &c, 1, MSG_PEEK | MSG_DONTWAIT);
    if (n == 0)
        return 1;                                   /* 收到 FIN，对端有序关闭 */
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return 0;                               /* 暂无数据，连接仍活 */
        return 1;                                   /* ECONNRESET 等，视为断开 */
    }
    return 0;                                       /* 有数据(本端不消费)，不算断开 */
}
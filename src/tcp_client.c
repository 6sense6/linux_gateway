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

    if (ip == NULL || port <= 0 || port > 65535) {
        LOG_ERROR("tcp: invalid ip/port argument");
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_ERROR("tcp: socket failed: %s", strerror(errno));
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);

    /* inet_pton 返回 1 才算成功；0=字符串不是合法 IPv4，-1=地址族不支持 */
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        LOG_ERROR("tcp: '%s' is not a valid IPv4 address", ip);
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        /* 最常见 errno=ECONNREFUSED：服务端没起来 */
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

    /* send 是"部分写"：可能返回小于 len，必须循环发满（和你 serial_write 一个套路） */
    while (total < len) {
        ssize_t n = send(sockfd, p + total, (size_t)(len - total), MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR)
                continue;
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
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "logger.h"
#include "serial.h"

int serial_open(const char *dev)
{
    int fd;
    struct termios tio;
    if (dev == NULL) {
        LOG_ERROR("serial_open: dev is NULL");
        return -1;
    }

    fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        LOG_ERROR("open %s failed: %s", dev, strerror(errno));
        return -1;
    }

    if (tcgetattr(fd, &tio) < 0) {
        LOG_ERROR("tcgetattr %s failed: %s", dev, strerror(errno));
        close(fd);
        return -1;
    }

    cfmakeraw(&tio);

    if (cfsetispeed(&tio, B115200) < 0) {
        LOG_ERROR("cfsetispeed failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    if (cfsetospeed(&tio, B115200) < 0) {
        LOG_ERROR("cfsetospeed failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    tio.c_cflag |= CLOCAL | CREAD;
    tio.c_cflag |= CS8;
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;

    /* 必须在 cfmakeraw 之后设置，否则会被它重置为 VMIN=1 VTIME=0 */
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 20;

    if (tcsetattr(fd, TCSANOW, &tio) < 0) {
        LOG_ERROR("tcsetattr %s failed: %s", dev, strerror(errno));
        close(fd);
        return -1;
    }

    LOG_INFO("serial %s opened, fd=%d, 115200 8N1, VMIN=0 VTIME=20", dev, fd);
    return fd;
}

int serial_read(int fd, void *buf, int len)
{
    int n;

    if (buf == NULL || len <= 0) {
        LOG_ERROR("serial_read: bad args");
        return -1;
    }

    while (1) {
        n = (int)read(fd, buf, (size_t)len);
        if (n >= 0) {
            return n;
        }
        if (errno == EINTR) {
            continue; /* 被信号打断，重读，不算错误 */
        }
        LOG_ERROR("read fd=%d failed: %s", fd, strerror(errno));
        return -1;
    }
}

int serial_write(int fd, const void *buf, int len)
{
    const char *p = (const char *)buf;
    int written = 0;

    if (buf == NULL || len <= 0) {
        LOG_ERROR("serial_write: bad args");
        return -1;
    }

    while (written < len) { /* write 可能只写一部分，必须循环写满 */
        int n = (int)write(fd, p + written, (size_t)(len - written));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG_ERROR("write fd=%d failed: %s", fd, strerror(errno));
            return -1;
        }
        if (n == 0) { /* 写了0字节不前进, 防死循环 */
            LOG_ERROR("write fd=%d wrote 0 bytes", fd);
            return -1;
        }
        written += n;
    }
    return written;
}

int serial_close(int fd)
{
    if (fd < 0) {
        LOG_ERROR("serial_close: bad fd %d", fd);
        return -1;
    }
    if (close(fd) < 0) {
        LOG_ERROR("close fd=%d failed: %s", fd, strerror(errno));
        return -1;
    }
    return 0;
}
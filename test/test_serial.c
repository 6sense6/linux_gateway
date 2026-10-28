#include <string.h>

#include "logger.h"
#include "serial.h"

#define BUF_LEN 256

int main(int argc, char *argv[])
{
    int rc = 0;
    int fd;
    int n;
    char buf[BUF_LEN];

    if (logger_init(NULL, LOG_LVL_DEBUG) != 0) {
        return 1;
    }

    if (argc < 2) {
        LOG_ERROR("usage: %s /dev/ttyXXX", argv[0]);
        return 1;
    }

    fd = serial_open(argv[1]);
    if (fd < 0) {
        return 1;
    }

    /* 开机先主动发一句，验证 serial_write 通路，对侧 cat 应能看到 */
    const char *hello = "hello from gateway\n";
    n = serial_write(fd, hello, strlen(hello));
    if (n < 0) {
        LOG_ERROR("serial_write failed");
        serial_close(fd);
        return 1;
    }
    LOG_INFO("sent %d bytes", n);

    while (1) {
        n = serial_read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            LOG_INFO("recv %d bytes: %s", n, buf);
        } else if (n == 0) {
            LOG_DEBUG("heartbeat: no data in 2s");
        } else {
            LOG_ERROR("serial_read failed, exit");
            rc = 1;
            break;
        }
    }

    serial_close(fd);
    LOG_INFO("test_serial exit");
    return rc;
}
#include <stdio.h>
#include <stdint.h>
#include "logger.h"
#include "serial.h"
#include "frame.h"

int main(void)
{
    int fd;
    int rc = 0;
    int total = 0;
    int n;
    int i;
    uint8_t buf[256];
    frame_parser_t parser;
    frame_t f;

    logger_init(NULL, LOG_LVL_DEBUG);

    fd = serial_open("/tmp/ttyGateway");
    if (fd < 0) {
        logger_close();
        return 1;
    }
    frame_parser_init(&parser);

    while (1) {
        n = serial_read(fd, buf, sizeof(buf));
        if (n < 0) {
            LOG_ERROR("gateway: serial broken, exit");
            rc = 1;
            break;
        }
        if (n == 0) {
            LOG_DEBUG("gateway: no data, keep waiting");
            continue;
        }
        for (i = 0; i < n; i++) {
            if (frame_parse_byte(&parser, buf[i], &f) == 1) {
                /* 温湿度帧约定 payload 固定 4 字节, 非 4 字节按未知帧跳过, 不硬取 */
                if (f.len != 4) {
                    LOG_WARN("gateway: frame len=%d != 4, skip", f.len);
                    continue;
                }
                int16_t temp = (int16_t)((f.payload[0] << 8) | f.payload[1]);
                int16_t humi = (int16_t)((f.payload[2] << 8) | f.payload[3]);
                total++;
                LOG_INFO("gateway: frame %d temp=%.1fC humi=%.1f%%",
                         total, temp / 10.0, humi / 10.0);
            }
        }
    }

    serial_close(fd);
    logger_close();
    return rc;
}
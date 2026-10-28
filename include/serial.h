#ifndef SERIAL_H
#define SERIAL_H

/*
 * serial 模块：termios 串口薄封装
 * 配置固定为 115200 8N1，VMIN=0 VTIME=20，即纯阻塞读、2 秒超时返回 0
 *
 * 返回值约定：
 *   serial_open   成功返回 fd，失败返回 -1
 *   serial_read   大于 0 为收到的字节数，0 为超时无数据（不是错误），-1 为错误
 *   serial_write  成功返回写出的字节数，失败返回 -1
 *   serial_close  成功返回 0，失败返回 -1
 */

int serial_open(const char *dev);
int serial_read(int fd, void *buf, int len);
int serial_write(int fd, const void *buf, int len);
int serial_close(int fd);

#endif /* SERIAL_H */
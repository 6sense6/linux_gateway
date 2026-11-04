#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

/* 成功返回 sockfd(>=0)，失败 -1；内部自带日志 */
int tcp_client_connect(const char *ip, int port);

/* 全量发送 len 字节：成功返回 len，失败 -1；内部已处理 EINTR 重试 */
int tcp_client_send(int sockfd, const void *buf, int len);

/* 关闭 socket，成功 0，失败 -1 */
int tcp_client_close(int sockfd);

#endif /* TCP_CLIENT_H */
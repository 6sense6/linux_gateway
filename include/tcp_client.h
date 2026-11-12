#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

/* 成功返回 sockfd(>=0)，失败 -1 */
int tcp_client_connect(const char *ip, int port);

/* 全量发送 len 字节：成功返回 len，失败 -1；内部处理 EINTR 重试 */
int tcp_client_send(int sockfd, const void *buf, int len);

/* 关闭 socket，成功 0，失败 -1 */
int tcp_client_close(int sockfd);

/* 探测对端是否已关闭：1=已断开(FIN/ECONNRESET)，0=仍连通/无数据 */
int tcp_client_peer_closed(int sockfd);

#endif /* TCP_CLIENT_H */
#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <pthread.h>
#include <stdint.h>

// 环形缓冲区结构体
typedef struct {
    uint8_t *buf;             // 数据存储区
    int size;                 // 总容量
    int r_idx;                // 读索引
    int w_idx;                // 写索引
    int abort;                // 终止标志位
    pthread_mutex_t mutex;    // 互斥锁：保护共享资源
    pthread_cond_t not_empty; // 条件变量：非空（可读）
    pthread_cond_t not_full;  // 条件变量：非满（可写）
} ringbuffer_t;

int  ringbuffer_init(ringbuffer_t *rb, int size);
void ringbuffer_destroy(ringbuffer_t *rb);
void ringbuffer_stop(ringbuffer_t *rb);
int  ringbuffer_is_stopped(ringbuffer_t *rb);
int  ringbuffer_write(ringbuffer_t *rb, uint8_t data);
int  ringbuffer_read(ringbuffer_t *rb, uint8_t *data);

#endif
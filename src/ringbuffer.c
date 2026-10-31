#include <stdlib.h>
#include <string.h>
#include "ringbuffer.h"

// 判断缓冲区满
static int is_full(ringbuffer_t *rb)
{
    return (rb->w_idx + 1) % rb->size == rb->r_idx;
}

// 判断缓冲区空
static int is_empty(ringbuffer_t *rb)
{
    return rb->w_idx == rb->r_idx;
}

// 初始化缓冲区
int ringbuffer_init(ringbuffer_t *rb, int size)
{
    if (!rb || size <= 0) return -1;

    rb->buf = malloc(size);
    if (!rb->buf) return -1;
    memset(rb->buf, 0, size);

    rb->size = size;
    rb->r_idx = 0;
    rb->w_idx = 0;
    rb->abort = 0;

    /* pthread_* 返回值必须查, 任一失败都要把已建好的资源逐层释放 */
    if (pthread_mutex_init(&rb->mutex, NULL) != 0) {
        free(rb->buf);
        return -1;
    }
    if (pthread_cond_init(&rb->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&rb->mutex);
        free(rb->buf);
        return -1;
    }
    if (pthread_cond_init(&rb->not_full, NULL) != 0) {
        pthread_cond_destroy(&rb->not_empty);
        pthread_mutex_destroy(&rb->mutex);
        free(rb->buf);
        return -1;
    }
    return 0;
}

// 销毁缓冲区，释放所有资源
void ringbuffer_destroy(ringbuffer_t *rb)
{
    if (!rb) return;
    pthread_mutex_destroy(&rb->mutex);
    pthread_cond_destroy(&rb->not_empty);
    pthread_cond_destroy(&rb->not_full);
    free(rb->buf);
}

// 停止缓冲区，广播唤醒所有阻塞线程
void ringbuffer_stop(ringbuffer_t *rb)
{
    if (!rb) return;
    pthread_mutex_lock(&rb->mutex);
    rb->abort = 1;
    pthread_cond_broadcast(&rb->not_empty);
    pthread_cond_broadcast(&rb->not_full);
    pthread_mutex_unlock(&rb->mutex);
}

// 写入一个字节，满则阻塞等待
int ringbuffer_write(ringbuffer_t *rb, uint8_t data)
{
    if (!rb) return -1;

    pthread_mutex_lock(&rb->mutex);

    // ✅ P180 面试考点：while 防止虚假唤醒
    while (is_full(rb) && !rb->abort) {
        // ✅ P176 考点：wait 原子操作：释放锁 → 阻塞 → 被唤醒 → 重新加锁
        pthread_cond_wait(&rb->not_full, &rb->mutex);
    }

    if (rb->abort) {
        pthread_mutex_unlock(&rb->mutex);
        return -1;
    }

    rb->buf[rb->w_idx] = data;
    rb->w_idx = (rb->w_idx + 1) % rb->size;

    pthread_cond_signal(&rb->not_empty);
    pthread_mutex_unlock(&rb->mutex);
    return 0;
}

// 读取一个字节，空则阻塞等待
int ringbuffer_read(ringbuffer_t *rb, uint8_t *data)
{
    if (!rb || !data) return -1;

    pthread_mutex_lock(&rb->mutex);

    while (is_empty(rb) && !rb->abort) {
        pthread_cond_wait(&rb->not_empty, &rb->mutex);
    }

    if (rb->abort && is_empty(rb)) {
        pthread_mutex_unlock(&rb->mutex);
        return -1;
    }

    *data = rb->buf[rb->r_idx];
    rb->r_idx = (rb->r_idx + 1) % rb->size;

    pthread_cond_signal(&rb->not_full);
    pthread_mutex_unlock(&rb->mutex);
    return 0;
}
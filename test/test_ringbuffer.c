#include <stdio.h>
#include <unistd.h>
#include <pthread.h>  // 确保包含了 pthread 头文件
#include "ringbuffer.h"

static int prod_count = 0, cons_count = 0, err_count = 0;

// 生产者线程：循环写入 A-Z
void *producer(void *arg)
{
    ringbuffer_t *rb = (ringbuffer_t *)arg;
    char c = 'A';

    while (1) {
        int ret = ringbuffer_write(rb, c);
        if (ret != 0) break;
        prod_count++;
        c++;
        if (c > 'Z') c = 'A';
    }
    printf("\n[生产者] 退出，共写入 %d\n", prod_count);

    printf("\n[生产者] 线程退出\n");
    pthread_exit(NULL);
}

// 消费者线程：循环读取并打印
void *consumer(void *arg)
{
    ringbuffer_t *rb = (ringbuffer_t *)arg;
    char c;
    char expected_next = 'A';

    while (1) {
        int ret = ringbuffer_read(rb, &c);
        if (ret != 0) break;

	cons_count++;
	usleep(50 * 1000);

        if (c != expected_next) {
            err_count++;
        }
        expected_next++;
        if (expected_next > 'Z') expected_next = 'A';
        
        printf("%c ", c);
        fflush(stdout);
    }
    printf("\n[消费者] 退出，共读取 %d\n", cons_count);

    printf("\n[消费者] 线程退出\n");
    pthread_exit(NULL);
}

int main(void)
{
    ringbuffer_t rb;
    pthread_t tid_prod, tid_cons;

    // 修改点 2：去掉 perror，改用 fprintf
    if (ringbuffer_init(&rb, 32) != 0) { 
        fprintf(stderr, "Error: ringbuffer_init failed.\n"); 
        return 1; 
    }
    
    printf("=== 程序启动，运行10秒 ===\n");

    // 修改点 1.1：第一个 create 失败，必须 destroy
    if (pthread_create(&tid_prod, NULL, producer, &rb) != 0) {
        fprintf(stderr, "Error: create producer thread failed.\n");
        ringbuffer_destroy(&rb); 
        return 1;
    }

    // 修改点 1.2：第二个 create 失败，需要优雅地把第一个线程杀掉再清理
    if (pthread_create(&tid_cons, NULL, consumer, &rb) != 0) {
        fprintf(stderr, "Error: create consumer thread failed.\n");
        
        // 绝不能用 pthread_join 直接死等，必须先发送停止信号
        ringbuffer_stop(&rb);          
        pthread_join(tid_prod, NULL);  // 此时生产者收到停止信号，才会退出，主线程才能 join 成功
        
        ringbuffer_destroy(&rb);       // 最后清理仓库
        return 1;
    }

    // 运行10秒
    sleep(10);

    printf("\n=== 时间到，停止程序 ===\n");
    ringbuffer_stop(&rb);

    // P156 等待线程退出，回收资源，避免僵尸线程
    pthread_join(tid_prod, NULL);
    pthread_join(tid_cons, NULL);

    printf("生产者写入：%d 个字符\n", prod_count);
    printf("消费者读取：%d 个字符\n", cons_count);
    printf("错误数量：%d 个\n", err_count);

    ringbuffer_destroy(&rb);

    if (err_count > 0 || prod_count != cons_count) {
        printf("\n 验证失败：顺序错误或丢数据\n");
        return 1;
    }
    printf("\n 验证完成：顺序正确、无丢数据、无崩溃\n");

    return 0;
}

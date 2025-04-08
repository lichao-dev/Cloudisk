#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <cloudisk.h>
#include "config.h"
#include "session.h"
#include "file_transfer.h"
#include "msg.h"

// 任务队列，每个元素组成
typedef struct{
    void (*func)(void *); // 任务函数
    void *arg; // 任务参数
}ThreadTask;

typedef struct{
    pthread_t *threads;; // 线程数组，存放线程tid，动态分配
    ThreadTask *task_queue; // 任务队列，动态分配
    int front; // 任务队列头指针
    int rear; // 任务队列尾指针
    int thread_num; // 线程池实际线程数
    int queue_size; // 任务队列实际大小
    int stop; // 是否停止线程池，0运行，1停止
    pthread_mutex_t mutex; // 互斥锁，保护线程池
    pthread_cond_t cond; // 条件变量
}ThreadPool;

// 线程池全局变量（在 thread_pool.c 中定义）
extern ThreadPool thread_pool;

// 线程池初始化
void thread_pool_init();
// 把任务加入线程池任务队列
int thread_pool_add_task(void (*function)(void *), void *arg);
// 销毁线程池，释放资源
void thread_pool_destroy();

#endif

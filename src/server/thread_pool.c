#include "thread_pool.h"

ThreadPool thread_pool;

// 线程工作函数
void *thread_worker(void *arg) {
    // 获取当前线程的tid
    pthread_t tid = pthread_self();
    while (1) {
        // 上锁
        pthread_mutex_lock(&thread_pool.mutex);
        // 如果队列为空，就等待任务
        while (thread_pool.front == thread_pool.rear && !thread_pool.stop) {
            // 调试信息：打印线程正在等待
            printf("Thread %lu waiting...\n", tid);
            pthread_cond_wait(&thread_pool.cond, &thread_pool.mutex);
        }
        // 如果因为要退出被唤醒
        if (thread_pool.stop) {
            pthread_mutex_unlock(&thread_pool.mutex);
            printf("Thread %lu: I am worker, I am going to exit.\n", tid);
            pthread_exit(NULL);
        }

        if (thread_pool.front != thread_pool.rear) {
            // 取出任务
            ThreadTask task = thread_pool.task_queue[thread_pool.front];
            thread_pool.front = (thread_pool.front + 1) % thread_pool.queue_size;
            // 解锁
            pthread_mutex_unlock(&thread_pool.mutex);
            // 执行任务
            CLOUDISK_LOG_INFO("Thread %lu is executing task", tid);
            task.func(task.arg);
        } else {
            pthread_mutex_unlock(&thread_pool.mutex);
        }
    }
    return NULL;
}

void thread_pool_init() {
    thread_pool.thread_num = config.thread_pool_size;
    thread_pool.queue_size = config.task_queue_size;

    thread_pool.threads = (pthread_t *)calloc(thread_pool.thread_num, sizeof(pthread_t));
    thread_pool.task_queue = (ThreadTask *)calloc(thread_pool.queue_size, sizeof(ThreadTask));
    // 创建线程
    for (int i = 0; i < thread_pool.thread_num; ++i) {
        pthread_create(&thread_pool.threads[i], NULL, thread_worker, NULL);
    }

    for (int i = 0; i < thread_pool.queue_size; ++i) {
        thread_pool.task_queue[i].func = NULL;
        thread_pool.task_queue[i].arg = NULL;
    }

    // 初始化其他成员
    thread_pool.front = 0;
    thread_pool.rear = 0;
    thread_pool.stop = 0;

    // 初始化互斥锁和条件变量
    pthread_mutex_init(&thread_pool.mutex, NULL);
    pthread_cond_init(&thread_pool.cond, NULL);

    CLOUDISK_LOG_INFO("The thread pool has been initialized");
}

// 把任务加入线程池任务队列
int thread_pool_add_task(void (*func)(void *), void *arg) {
    // 上锁
    pthread_mutex_lock(&thread_pool.mutex);
    // 判队满，队列如果满了，就由调用thread_pool_add_task的函数向客户端发送反馈消息
    // 采用循环队列，牺牲一个存储单元的思想
    if ((thread_pool.rear + 1) % thread_pool.queue_size == thread_pool.front) {
        printf("Queue is full!\n");
        pthread_mutex_unlock(&thread_pool.mutex);
        return -1;
    }

    // 如果队列未满，则添加相应的任务到任务队列
    thread_pool.task_queue[thread_pool.rear].func = func;
    thread_pool.task_queue[thread_pool.rear].arg = arg;
    thread_pool.rear = (thread_pool.rear + 1) % thread_pool.queue_size;

    // 唤醒因为队列空(task.func)(arg)阻塞的线程
    pthread_cond_signal(&thread_pool.cond);
    // 解锁
    pthread_mutex_unlock(&thread_pool.mutex);

    CLOUDISK_LOG_INFO("The task has been successfully added to the task queue");
    return 0;
}

// 关闭线程池
void thread_pool_destroy() {
    // 上锁
    pthread_mutex_lock(&thread_pool.mutex);
    // 设置停止标志位
    thread_pool.stop = 1;
    // 唤醒所有线程
    pthread_cond_broadcast(&thread_pool.cond);
    pthread_mutex_unlock(&thread_pool.mutex);

    // 等待每个线程终止
    for (int i = 0; i < thread_pool.thread_num; ++i) {
        pthread_join(thread_pool.threads[i], NULL);
    }

    // 释放资源
    free(thread_pool.threads);
    free(thread_pool.task_queue);
    // 销毁锁
    pthread_mutex_destroy(&thread_pool.mutex);
    // 销毁条件变量
    pthread_cond_destroy(&thread_pool.cond);

    CLOUDISK_LOG_INFO("The thread pool is closed");
}

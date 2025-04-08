#ifndef MSG_H
#define MSG_H

#include <cloudisk.h>

#define MSG_LEN 256     // 错误消息的数组长度
#define DATA_SIZE 4096

// 小火车，传输数据
typedef struct {
    int length;  // 火车头，存储数据长度
    char data[DATA_SIZE]; // 数据
} Train;

// 发送自定义命令反馈给客户端
int send_msg(int sockfd, const char *msg);
// 发送error的错误指示给客户端,prefix是消息前缀
int send_err_msg(int sockfd, const char *prefix);
// 循环接收n字节
int recvn(int sockfd, void *buf, ssize_t n);

#endif

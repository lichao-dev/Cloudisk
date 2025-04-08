#include "msg.h"

// 循环接收n字节
int recvn(int sockfd, void *buf, ssize_t n) {
    ssize_t sret;
    char *p = (char *)buf; // void *不能做偏移
    ssize_t cursize = 0;
    while (cursize < n) {
        sret = recv(sockfd, p + cursize, n - cursize, 0);
        if (sret <= 0) {
            return sret;
        }
        cursize += sret;
    }
    return cursize;
}

// 发送自定义命令反馈给客户端
int send_msg(int sockfd, const char *msg) {
    Train train;
    train.length = strlen(msg);
    memcpy(train.data, msg, train.length);
    int ret = send(sockfd, &train, sizeof(train.length) + train.length, 0);
    ERROR_CHECK(ret, -1, "send msg");

    return 0;
}

// 发送error的错误指示给客户端,prefix是消息前缀
int send_err_msg(int sockfd, const char *prefix) {
    char err_msg[MSG_LEN];
    snprintf(err_msg, sizeof(err_msg), "%s: %s\n", prefix, strerror(errno));
    send_msg(sockfd, err_msg);
    return 0;
}

#ifndef SERVER_INIT_H
#define SERVER_INIT_H

#include "config.h"
#include <cloudisk.h>
// 初始化TCP
int tcp_init(char *ip, int port);
// 添加监听
int epoll_add(int epfd, int fd);
// 解除监听
int epoll_del(int epfd, int fd);

#endif

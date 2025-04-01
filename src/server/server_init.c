#include "server_init.h"

int tcp_init(char *ip,int port) {

    // 创建套接字
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    ERROR_CHECK(sockfd, -1, "socket");

    // 设置服务器地址
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    // 允许端口重用
    int opt = 1;
    int ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ERROR_CHECK(ret, -1, "setsockopt");

    // 绑定套接字
    int bret = bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    ERROR_CHECK(bret, -1, "bind");

    // 监听套接字
    int lret = listen(sockfd, 50);
    ERROR_CHECK(lret, -1, "listen");

    return sockfd;
}

// 添加监听
int epoll_add(int epfd, int fd) {
    struct epoll_event ev; // 将来就绪以后要放入就绪集合里面的内容
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);

    return 0;
}

// 解除监听
int epoll_del(int epfd, int fd) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
    return 0;
}

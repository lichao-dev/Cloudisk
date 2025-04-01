#include "client.h"

int main(int argc, char *argv[]) {
    //./client ip port
    ARGS_CHECK(argc, 4);

    // 设置服务器地址
    // 控制连接
    int control_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    ERROR_CHECK(control_sockfd, -1, "socket");
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    // 连接服务器
    int ret = connect(control_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    ERROR_CHECK(ret, -1, "Connect server control");
    printf("Server control connected.\n");
    // 向服务端发送用户名
    ssize_t send_ret = send(control_sockfd, argv[3], strlen(argv[3]), 0);
    ERROR_CHECK(send_ret, -1, "send username");
    // 输入密码
    char *passwd;
    passwd = getpass("Please enter your password to log in: ");
    if (passwd == NULL) {
        perror("enter password");
        close(control_sockfd);
        exit(1);
    }
    // 发送密码
    send_ret = send(control_sockfd, passwd, strlen(passwd), 0);
    if (send_ret == -1) {
        perror("send password");
        close(control_sockfd);
        exit(1);
    }
    // 接收是否登录成功的消息
    char login_msg[BUF_SIZE] = {0};
    ssize_t n = recv(control_sockfd, login_msg, sizeof(login_msg) - 1, 0);
    if (n <= 0) {
        printf("recv login msg failed\n");
        close(control_sockfd);
        exit(1);
    }
    login_msg[n] = '\0';
    printf("%s", login_msg);

    // 接收服务端发送的token
    char token[TOKEN_LEN] = {0};
    n = recv(control_sockfd, token, sizeof(token) - 1, 0);
    if (n <= 0) {
        printf("Failed to receive token from server\n");
        close(control_sockfd);
        exit(1);
    }
    token[n] = '\0';

    // 连接数据通道
    int data_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    ERROR_CHECK(control_sockfd, -1, "socket");
    server_addr.sin_port = htons(DATA_PORT);
    ret = connect(data_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    ERROR_CHECK(ret, -1, "Connect server data");
    // 发送token
    send_ret = send(data_sockfd, token, strlen(token), 0);
    if (send_ret == -1) {
        perror("send token");
        close(data_sockfd);
    }
    printf("Server data connected.\n");

    // 创建epoll文件对象
    int epfd = epoll_create(1);
    // 增加监听
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = STDIN_FILENO;
    epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev);

    ev.events = EPOLLIN;
    ev.data.fd = control_sockfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, control_sockfd, &ev);

    struct epoll_event events[MAX_EVENTS];
    char buf[BUF_SIZE] = {0};
    while (1) {
        int rdnum = epoll_wait(epfd, events, MAX_EVENTS, -1);
        ERROR_CHECK(rdnum, -1, "epoll_wait");

        for (int i = 0; i < rdnum; ++i) {
            int curr_fd = events[i].data.fd;
            if (curr_fd == STDIN_FILENO) { // 发送命令到客户端
                memset(buf, 0, BUF_SIZE);
                ssize_t n = read(STDIN_FILENO, buf, BUF_SIZE);
                ERROR_CHECK(n, -1, "read stdin");
                if (n == 0) {
                    // Ctrl+D退出客户端
                    printf("Client quit!\n");
                    close(control_sockfd);
                    exit(0);
                }
                buf[strcspn(buf, "\n")] = '\0'; // 去掉末尾换行符
                printf("buf = %s\n", buf);

                // 判断是否是gets或puts
                char input[BUF_SIZE];
                strcpy(input, buf);
                char *tokens[TOKEN_NUM]; // 存放分解的token，token包括命令、参数
                int count = 0;
                // strtok的返回值是token的指针，如果没有更多token返回NULL
                char *str = strtok(input, " \t\n");
                while (str != NULL && count < TOKEN_NUM) {
                    tokens[count++] = str;
                    // 传入NULL是告诉strtok继续从上一次结束的地方扫描
                    str = strtok(NULL, " \t\n");
                }
                // 如果是gets/puts命令
                if (strcmp(tokens[0], "gets") == 0 || strcmp(tokens[0], "puts") == 0) {
                    int sret = send(control_sockfd, buf, n, 0);
                    ERROR_CHECK(sret, -1, "send msg");
                    // 调用封装函数启动传输线程
                    start_transfer_thread(data_sockfd, tokens, count);
                } else { // 其他命令
                    int sret = send(control_sockfd, buf, n, 0);
                    ERROR_CHECK(sret, -1, "send msg");
                }
            }
            // 接收服务端消息
            if (curr_fd == control_sockfd) {
                memset(buf, 0, BUF_SIZE);
                ssize_t n = recv(control_sockfd, buf, sizeof(buf) - 1, 0);
                ERROR_CHECK(n, -1, "recv msg");
                if (n == 0) {
                    printf("Server disconnected!\n");
                    exit(EXIT_FAILURE);
                }
                buf[n] = '\0';
                printf("%s", buf);
            }
        }
    }

    close(control_sockfd);
    close(data_sockfd);

    return 0;
}

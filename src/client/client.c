#include "client.h"

int main(int argc, char *argv[]) {
    // 使用方法
    // 注册: ./client <ip> <port> register <username> <password>
    // 登录: ./client <ip> <port> login <username>
    if (argc < 5) {
        fprintf(stderr,
                "Usage:\n  For register: %s <server_ip> <port> register <username> <password>\n"
                "  For login:   %s <server_ip> <port> login <username>\n",
                argv[0], argv[0]);
        exit(1);
    }

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

    char *mode = argv[3];
    char *username = argv[4];
    Train train;
    // 1.发送模式("register"或"login");
    train.length = strlen(mode);
    memcpy(train.data, mode, train.length);
    ret = send(control_sockfd, &train, sizeof(train.length) + train.length, 0);
    ERROR_CHECK(ret, -1, "send mode");

    // 注册
    if (strcmp(mode, "register") == 0) {
        if (argc < 6) {
            fprintf(stderr, "For registration, please provide <password>\n");
            close(control_sockfd);
            exit(1);
        }
        char *password = argv[5];
        // 调用注册函数
        client_register(control_sockfd, mode, username, password);
    }
    // 登录
    if (strcmp(mode, "login") == 0) {
        // 调用登录函数
        client_login(control_sockfd, username);
    } else {
        // 不是登录模式就退出
        printf("Please select one of the registration or login mode\n");
        close(control_sockfd);
        exit(0);
    }

    // 接收服务端发送的token
    char token[TOKEN_LEN] = {0};
    ssize_t n = recvn(control_sockfd, &train.length, sizeof(train.length));
    if (n <= 0) {
        perror("recv server token length");
        close(control_sockfd);
        exit(1);
    }
    n = recvn(control_sockfd, token, train.length);
    if (n <= 0) {
        perror("recv server token");
        close(control_sockfd);
        exit(1);
    }
    token[n] = '\0';
    printf("Received toekn: %s\n", token);

    // 连接数据通道
    int data_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    ERROR_CHECK(control_sockfd, -1, "socket");
    server_addr.sin_port = htons(DATA_PORT);
    ret = connect(data_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    ERROR_CHECK(ret, -1, "Connect server data");
    // 发送token
    train.length = strlen(token);
    memcpy(train.data, token, train.length);
    ssize_t send_ret = send(data_sockfd, &train, sizeof(train.length) + train.length, 0);
    if (send_ret == -1) {
        perror("send token");
        close(data_sockfd);
        exit(1);
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
                    train.length = strlen(buf);
                    memcpy(train.data, buf, train.length);
                    ret = send(control_sockfd, &train, sizeof(train.length) + train.length, 0);
                    ERROR_CHECK(ret, -1, "send msg");
                    // 调用封装函数启动传输线程
                    start_transfer_thread(data_sockfd, tokens, count);
                } else { // 其他命令
                    train.length = strlen(buf);
                    memcpy(train.data, buf, train.length);
                    ret = send(control_sockfd, &train, sizeof(train.length) + train.length, 0);
                    ERROR_CHECK(ret, -1, "send msg");
                }
            }
            // 接收服务端消息
            if (curr_fd == control_sockfd) {
                memset(buf, 0, BUF_SIZE);
                n = recvn(control_sockfd, &train.length, sizeof(train.length));
                if (n <= 0) {
                    perror("recv server msg length");
                    close(control_sockfd);
                    close(data_sockfd);
                    exit(1);
                }
                n = recvn(control_sockfd, buf, train.length);
                if (n <= 0) {
                    perror("recv server msg");
                    close(control_sockfd);
                    close(data_sockfd);
                    exit(1);
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

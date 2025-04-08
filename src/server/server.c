#include "server.h"

int pipe_fd[2]; // 因为handler函数要使用，所以须为全局变量
// 信号处理函数
void handler(int signum) {
    printf("signum = %d\n", signum);
    // 随便写入什么数据，只是为了让子进程监听到管道有动静
    ssize_t ret = write(pipe_fd[1], "exit", 4);
    ERROR_CHECK(ret, -1, "write pipe");
    CLOUDISK_LOG_ERR("Triggering an exit signal");
}

int main(int argc, char *argv[]) {

    // ./server server.conf
    ARGS_CHECK(argc, 2);

    // 打开syslog，标识符为"Cloudisk",记录进程ID
    openlog("Cloudisk", LOG_PID | LOG_CONS, LOG_USER);
    // 创建父子进程通信的匿名管道
    pipe(pipe_fd);
    if (fork()) {
        // 父进程
        close(pipe_fd[0]);        // 父进程关闭读端
        signal(SIGINT, handler);  // 注册SIGINT信号
        signal(SIGTERM, handler); // 注册SIGTERM信号
        wait(NULL);               // 等待子进程终止
        close(pipe_fd[1]);        // 关闭文件描述符
        printf("Parent process is going to exit.\n");
        CLOUDISK_LOG_INFO("The parent process is about to exit");
        closelog();
        exit(EXIT_SUCCESS);
    } else {
        // 子进程
        close(pipe_fd[1]); // 子进程关闭写端
        
        signal(SIGPIPE, SIG_IGN);
        signal(SIGINT, SIG_IGN);  // 忽略 SIGINT 信号
        signal(SIGTERM, SIG_IGN); // 忽略 SIGTERM 信号

        // 加载配置文件
        load_config(argv[1]);
        // 初始化线程池
        thread_pool_init();
        // 初始化数据库
        MYSQL *conn_db = db_init(config.db_host, config.db_user, config.db_pass, config.db_name);
        if (conn_db == NULL) {
            fprintf(stderr, "Failed to initialize database connection.\n");
            exit(1);
        }
        // 创建表
        if (db_create_tables(conn_db) != 0) {
            fprintf(stderr, "Failed to create tables.\n");
            db_close(conn_db);
            exit(1);
        }

        // 初始化TCP
        int control_fd = tcp_init(config.ip, config.control_port); // 控制连接
        printf("Server control socket is listening...\n");
        CLOUDISK_LOG_INFO("Server control socket is listening...");
        int data_fd = tcp_init(config.ip, config.data_port); // 控制连接
        printf("Server data socket is listening...\n");
        CLOUDISK_LOG_INFO("Server data socket is listening...");
        // 创建一个结构体数组保存客户端信息
        UserInfo user[config.max_connections];
        memset(&user, 0, sizeof(user));
        int user_count = 0; // 已连接的用户数
                            //
        // 创建epoll文件对象
        int epfd = epoll_create(1);
        // 监听控制连接
        epoll_add(epfd, control_fd);
        // 监听数据连接
        epoll_add(epfd, data_fd);
        // 将管道读端加入监听
        epoll_add(epfd, pipe_fd[0]);

        // 申请一个数组，用来保存返回的就绪事件
        struct epoll_event events[MAX_EVENTS];
        char cmd_buf[CMD_LEN] = {0}; // 用来接收客户端发来的命令

        while (1) {
            int rdnum = epoll_wait(epfd, events, MAX_EVENTS, -1);
            ERROR_CHECK(rdnum, -1, "epoll_wait");

            for (int i = 0; i < rdnum; ++i) {
                int curr_fd = events[i].data.fd;
                if (curr_fd == control_fd) {
                    struct sockaddr_in client_addr;
                    socklen_t addr_len = sizeof(client_addr);
                    int new_fd = accept(control_fd, (struct sockaddr *)&client_addr, &addr_len);
                    ERROR_CHECK(new_fd, -1, "accept");
                    CLOUDISK_LOG_INFO("Receive client control connection request from %s:%d",
                                      inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                    if (user_count >= config.max_connections) {
                        send_msg(new_fd,
                                 "Sorry, the connection is full. Please try again later.\n");
                        CLOUDISK_LOG_INFO(
                            "Rejected connection from %s:%d due to max connections reached",
                            inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                        close(new_fd);
                        continue;
                    }

                    // 设置新连接的用户的信息
                    for (int j = 0; j < config.max_connections; ++j) {
                        if (user[j].state == DISCONNECTION) {
                            if (handle_control_connection(&user[j], epfd, new_fd, conn_db) == 0) {
                                user_count++;
                                printf("The control connection of %s has been established from "
                                       "%s:%d\n",
                                       user[j].username, inet_ntoa(client_addr.sin_addr),
                                       ntohs(client_addr.sin_port));
                                CLOUDISK_LOG_INFO(
                                    "The control connection of %s has been established from "
                                    "%s:%d",
                                    user[j].username, inet_ntoa(client_addr.sin_addr),
                                    ntohs(client_addr.sin_port));
                                printf("user_count = %d\n", user_count);
                            } else {
                                printf("ERROR: The control connection failed\n");
                                CLOUDISK_LOG_ERR("ERROR: The control connection failed from %s:%d",
                                                 inet_ntoa(client_addr.sin_addr),
                                                 ntohs(client_addr.sin_port));
                                close(new_fd);
                            }
                        break;
                        }
                    }
                    continue;
                }

                // 处理数据连接
                if (curr_fd == data_fd) {
                    struct sockaddr_in client_addr;
                    socklen_t addr_len = sizeof(client_addr);
                    int new_fd = accept(data_fd, (struct sockaddr *)&client_addr, &addr_len);
                    ERROR_CHECK(new_fd, -1, "accept");
                    // 调用数据连接处理函数，成功则返回该用户的数组下标
                    int idx = handle_data_connection(user, new_fd);
                    if (idx != -1) {
                        printf("The data connection of %s has been established from %s:%d\n",
                               user[idx].username, inet_ntoa(client_addr.sin_addr),
                               ntohs(client_addr.sin_port));
                        CLOUDISK_LOG_INFO(
                            "The data connection of %s has been established from %s:%d",
                            user[idx].username, inet_ntoa(client_addr.sin_addr),
                            ntohs(client_addr.sin_port));
                    } else {
                        // 数据连接失败，关闭连接
                        send_msg(new_fd, "Token verification failed\n");
                        CLOUDISK_LOG_ERR("Data connection token verification failed");
                        close(new_fd);
                    }
                    continue;
                }

                // 处理来自客户端的命令
                for (int j = 0; j < config.max_connections; ++j) {
                    int conn_fd = user[j].control_sockfd;
                    if (curr_fd == conn_fd) {
                        memset(cmd_buf, 0, sizeof(cmd_buf));
                        Train train;
                        ssize_t n = recvn(conn_fd, &train.length, sizeof(train.length));
                        ERROR_CHECK(n, -1, "recv msg length");
                        n = recvn(conn_fd, cmd_buf, train.length);
                        ERROR_CHECK(n, -1, "recv msg");
                        if (n == 0) {
                            printf("%s disconnected.\n", user[j].username);
                            CLOUDISK_LOG_INFO("%s disconnected", user[j].username);
                            close(conn_fd);
                            memset(&user[j], 0, sizeof(user[j]));
                            continue;
                        }
                        cmd_buf[n] = '\0';
                        printf("server.c cmd_buf = %s\n", cmd_buf);
                        cmd_parse(&user[j], cmd_buf, conn_db);
                    }
                }

                // 处理线程池退出
                if (curr_fd == pipe_fd[0]) {
                    printf("Thread pool is going to exit.\n");
                    CLOUDISK_LOG_INFO("Thread pool is going to exit");
                    thread_pool_destroy(); // 关闭线程池，释放资源
                    printf("Main thread/Child process is going to exit.\n");
                    CLOUDISK_LOG_INFO("Main thread/Child process is going to exit");
                    close(control_fd);
                    close(data_fd);
                    close(pipe_fd[0]);
                    // 关闭所有用户的文件描述符
                    for (int j = 0; j < config.max_connections; ++j) {
                        if (user[j].control_sockfd != 0) {
                            close(user[j].control_sockfd);
                            if (user[j].control_sockfd != -1) {
                                close(user[j].data_sockfd);
                            }
                        }
                    }
                    closelog();
                    db_close(conn_db);
                    exit(EXIT_SUCCESS); // 子进程即主线程退出
                }
            }
        }
    }

    return 0;
}

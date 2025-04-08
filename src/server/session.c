#include "session.h"

// 简单生成 token（例如：时间戳加随机数）
void generate_token(char *token, size_t size) {
    snprintf(token, size, "%ld_%d", time(NULL), rand());
}

// 处理控制连接
int handle_control_connection(UserInfo *user, int epfd, int control_sockfd, MYSQL *conn) {
    char mode[16] = {0};
    Train train;
    // 循环等待模式命令，允许用户先注册后登录
    while (1) {
        // 接收模式
        ssize_t n = recvn(control_sockfd, &train.length, sizeof(train.length));
        if (n <= 0) {
            perror("recv mode length");
            return -1;
        }
        n = recvn(control_sockfd, mode, train.length);
        if (n <= 0) {
            perror("recv mode");
            return -1;
        }
        mode[n] = '\0';
        printf("mode = %s\n", mode);
        CLOUDISK_LOG_INFO("Received mode: %s", mode);
        if (strcmp(mode, "register") == 0) {
            // 注册流程：接收用户名和密码，调用 register_user
            char username[USERNAME_LEN] = {0};
            char password[PASSWD_LEN] = {0};
            // 接收用户名
            n = recvn(control_sockfd, &train.length, sizeof(train.length));
            if (n <= 0) {
                perror("recv username length");
                return -1;
            }
            n = recvn(control_sockfd, username, train.length);
            if (n <= 0) {
                perror("recv username");
                return -1;
            }
            username[n] = '\0';
            // 接收密码
            n = recvn(control_sockfd, &train.length, sizeof(train.length));
            if (n <= 0) {
                perror("recv password length");
                return -1;
            }
            n = recvn(control_sockfd, password, train.length);
            if (n <= 0) {
                perror("recv password");
                return -1;
            }
            password[n] = '\0';
            // 调用注册函数
            int register_ret;
            if ((register_ret = register_user(conn, control_sockfd, username, password)) == 0) {
                send_msg(control_sockfd,
                         "Registration Successful. Please enter 'login' to proceed.\n");
            } else if (register_ret == 1) {
                // 用户存在，继续接收用户是否选择登录
                continue;
            } else {
                return -1;
            }
            // 继续循环，等待新的模式命令
            continue;
        } else if (strcmp(mode, "login") == 0) {
            // 登录流程：接收用户名，发送 salt，再接收加密密码
            char username[USERNAME_LEN] = {0};
            n = recvn(control_sockfd, &train.length, sizeof(train.length));
            if (n <= 0) {
                perror("recv username length");
                return -1;
            }
            n = recvn(control_sockfd, username, train.length);
            username[n] = '\0';
            // 获取盐值
            char salt[FULL_SALT_LEN] = {0};
            if (get_user_salt(conn, username, salt, sizeof(salt)) != 0) {
                send_msg(control_sockfd, "User not found or error retrieving salt");
                return -1;
                ;
            }
            // 将 salt 发送给客户端
            train.length = strlen(salt);
            memcpy(train.data, salt, train.length);
            ssize_t ret = send(control_sockfd, &train, sizeof(train.length) + train.length, 0);
            if (ret < 0) {
                perror("send salt");
                send_err_msg(control_sockfd, "send salt");
                return -1;
            }
            // 接收客户端发送的加密后的密码密文
            char client_encrypted[HASH_LEN] = {0};
            n = recvn(control_sockfd, &train.length, sizeof(train.length));
            printf("recv client_encrypted length: %d\n", train.length);
            n = recvn(control_sockfd, client_encrypted, train.length);
            if (n <= 0) {
                perror("recv encrypted password");
                send_err_msg(control_sockfd, "recv encrypted password");
                return -1;
            }
            client_encrypted[n] = '\0';
            printf("client_encrypted: %s\n", client_encrypted);

            // 调用登录函数
            if (login_user(conn, control_sockfd, username, client_encrypted) == 0) {
                send_msg(control_sockfd, "Login Successful\n");
                CLOUDISK_LOG_INFO("User %s login successful", username);
                strncpy(user->username, username, sizeof(username) - 1);
                break;
            } else {
                return -1;
            }
        } else {
            send_msg(control_sockfd, "Unknown mode. Please enter 'register' or 'login'.\n");
            return -1;
        }
    }

    // 设置用户信息
    user->control_sockfd = control_sockfd;
    user->data_sockfd = -1;
    user->state = CONNECTION;
    // 将每个客户端的当前目录都初始化为'/+用户名',表示该用户的根目录
    snprintf(user->current_dir, sizeof(user->current_dir), "/%s", user->username);

    // 设置用户上传目录
    snprintf(user->upload_dir, sizeof(user->upload_dir), "%s", config.upload_dir);

    // 可选：检查数据库中是否存在该用户的虚拟根目录记录
    // 这里假设每个用户的虚拟根目录 parent_id 均为 0
    int user_id = get_user_id(conn, user->username);
    if (user_id > 0) {
        FileRecord *records = NULL;
        int record_count = 0;
        // 查询当前用户在根目录（parent_id == 0）下的所有记录
        if (file_db_list_records(conn, user_id, 0, &records, &record_count) == 0) {
            // 如果查询结果为空，则说明还没有为该用户创建虚拟根目录记录
            if (record_count == 0) {
                FileRecord root;
                memset(&root, 0, sizeof(root));
                root.user_id = user_id;
                root.parent_id = 0; // 根目录
                // 用用户名作为目录名称
                strncpy(root.filename, user->username, FILENAME_SIZE - 1);
                // 虚拟根目录路径设置为 "/username"
                snprintf(root.path, PATH_SIZE, "/%s", user->username);
                root.type = 'd'; // 'd' 表示目录
                root.filesize = 0;
                root.sha256[0] = '\0'; // 目录无哈希值

                // 创建虚拟根目录记录到数据库中
                if (file_db_create_record(conn, &root) != 0) {
                    CLOUDISK_LOG_ERR("Failed to create virtual root directory for user %s",
                                     user->username);
                    // 根据实际需求决定是否视为致命错误，这里我们仅记录日志
                }
            }
            // 释放查询结果内存（如果有分配的话）
            if (records)
                free(records);
        }
    }

    // 生成token并发送给客户端
    generate_token(user->token, sizeof(user->token));
    train.length = strlen(user->token);
    memcpy(train.data, user->token, train.length);
    ssize_t sret = send(control_sockfd, &train, sizeof(train.length) + train.length, 0);
    if (sret == -1) {
        perror("send token");
        CLOUDISK_LOG_ERROR_CHECK("send token");
        return -1;
    }

    // 登录成功后，将控制连接加入 epoll 监听，并返回
    epoll_add(epfd, control_sockfd);
    return 0;
}

// 处理数据连接
int handle_data_connection(UserInfo user[], int data_sockfd) {
    // 接收客户端从数据通道发回的token
    char token[TOKEN_LEN] = {0};
    Train train;
    ssize_t n = recvn(data_sockfd, &train.length, sizeof(train.length));
    if (n <= 0) {
        perror("recv client token length");
        return -1;
    }
    n = recvn(data_sockfd, token, train.length);
    if (n <= 0) {
        perror("recv client token");
        CLOUDISK_LOG_ERR("%s: Failed to received token from client", user->username);
        return -1;
    }
    token[n] = '\0';

    // 逐个遍历比较已连接用户的token
    for (int i = 0; i < config.max_connections; ++i) {
        if (strcmp(user[i].token, token) == 0) {
            user[i].data_sockfd = data_sockfd;
            return i; // 匹配到则返回下标
        }
    }
    return -1; // 未匹配到返回-1
}

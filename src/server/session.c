#include "session.h"

// 发送自定义命令反馈给客户端
int send_msg(int sockfd, const char *msg) {
    int ret = send(sockfd, msg, strlen(msg), 0);
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

// 下面是处理控制连接和数据连接的部分

// 获取salt
void get_salt(char *salt, char *crypt_str) {
    // 查找第三个'$'的位置
    // char *strrchr(const char *s, int c)
    // 返回最后一个字符c的指针
    const char *third_dollar = strrchr(crypt_str, '$');
    if (third_dollar == NULL) {
        salt[0] = '\0'; // 格式错误
        return;
    }
    // 计算salt长度
    size_t len = third_dollar - crypt_str;
    // 复制salt
    strncpy(salt, crypt_str, len);
    salt[len] = '\0';
}

// 简单生成 token（例如：时间戳加随机数）
void generate_token(char *token, size_t size) {
    snprintf(token, size, "%ld_%d", time(NULL), rand());
}

// 处理控制连接
int handle_control_connection(UserInfo *user, int epfd, int control_sockfd) {
    // 登录验证
    // 获取用户名,先用临时数组来存储
    char temp_name[USERNAME_LEN] = {0};
    ssize_t ret = recv(control_sockfd, temp_name, sizeof(temp_name) - 1, 0);
    if (ret == -1) {
        perror("recv username");
        CLOUDISK_LOG_ERROR_CHECK("recv username");
        return -1;
    }
    temp_name[ret] = '\0';
    // 密码验证
    struct spwd *sp;
    char salt[SALT_LEN] = {0};
    // 从 /etc/shadow 获取指定用户的信息
    sp = getspnam(temp_name);
    if (sp == NULL) {
        send_err_msg(control_sockfd, "getspnam");
        CLOUDISK_LOG_ERROR_CHECK("getspnam");
        return -1;
    }
    // 获取salt
    get_salt(salt, sp->sp_pwdp);
    // 接收用户密码
    char passwd[PASSWD_LEN] = {0};
    ret = recv(control_sockfd, passwd, sizeof(passwd) - 1, 0);
    if (ret == -1) {
        perror("recv password");
        CLOUDISK_LOG_ERROR_CHECK("recv password");
        return -1;
    }
    // 验证
    if (strcmp(sp->sp_pwdp, crypt(passwd, salt)) == 0) {
        send_msg(control_sockfd, "Login Successful\n");
        CLOUDISK_LOG_INFO("User %s Login Successful", user->username);
    } else {
        send_msg(control_sockfd, "Login Failed\n");
        CLOUDISK_LOG_INFO("User %s Login Failed", user->username);
        return -1;
    }

    // 获取服务端的默认目录
    char default_dir[PATH_MAX] = {0};
    getcwd(default_dir, sizeof(default_dir));
    // 设置用户信息
    user->control_sockfd = control_sockfd;
    user->data_sockfd = -1;
    user->state = CONNECTION;
    // 将每个客户端的当前目录都初始化为服务端的当前目录
    strcpy(user->current_dir, default_dir);

    // 将收到的用户名保存到结构体中
    strcpy(user->username, temp_name);
    // 设置用户上传目录
    snprintf(user->upload_dir, sizeof(user->upload_dir), "%s%s", config.upload_dir, user->username);

    // 生成token并发送给客户端
    generate_token(user->token, sizeof(user->token));
    ssize_t sret = send(control_sockfd, user->token, strlen(user->token), 0);
    if (sret == -1) {
        perror("send token");
        CLOUDISK_LOG_ERROR_CHECK("send token");
        return -1;
    }

    // 将该控制连接加入 epoll 监听，便于后续读取客户端发送的控制命令
    epoll_add(epfd, user->control_sockfd);

    return 0;
}

// 处理数据连接
int handle_data_connection(UserInfo user[], int data_sockfd) {
    // 接收客户端从数据通道发回的token
    char token[TOKEN_LEN] = {0};
    int n = recv(data_sockfd, token, sizeof(token) - 1, 0);
    if (n <= 0) {
        printf("Failed to received token from client\n");
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

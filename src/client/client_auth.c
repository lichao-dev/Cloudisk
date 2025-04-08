#include "client_auth.h"

// 客户端注册
int client_register(int sockfd, char *mode, char *username, char *password) {
    Train train;
    // 发送用户名
    train.length = strlen(username);
    memcpy(train.data, username, train.length);
    ssize_t ret = send(sockfd, &train, sizeof(train.length) + train.length, 0);
    ERROR_CHECK(ret, -1, "send username");
    // 发送密码（明文，由服务端加密保存）
    train.length = strlen(password);
    memcpy(train.data, password, train.length);
    ret = send(sockfd, &train, sizeof(train.length) + train.length, 0);
    ERROR_CHECK(ret, -1, "send password");
    // 接收服务端返回的注册结果
    char response[BUF_SIZE];
    ssize_t n = recvn(sockfd, &train.length, sizeof(train.length));
    if (n <= 0) {
        perror("recv register response length");
        close(sockfd);
        exit(1);
    }
    n = recvn(sockfd, response, train.length);
    if (n <= 0) {
        perror("recv register response");
        close(sockfd);
        exit(1);
    }
    response[n] = '\0';
    printf("register response: %s", response);
    // 提示用户是否继续登录
    char choice[8] = {0};
    printf("Do want to login now? (y/n): ");
    fgets(choice, sizeof(choice), stdin);
    if (choice[0] == 'y' || choice[0] == 'Y') {
        // 切换到登录模式
        strcpy(mode, "login");
        train.length = strlen(mode);
        memcpy(train.data, mode, train.length);
        ret = send(sockfd, &train, sizeof(train.length) + train.length, 0);
        ERROR_CHECK(ret, -1, "send login mode");
    } else {
        printf("Exiting client.\n");
        close(sockfd);
        exit(0);
    }
    return 0;
}

// 客户端登录
int client_login(int sockfd, char *username) {
    // 发送用户名
    Train train;
    train.length = strlen(username);
    memcpy(train.data, username, train.length);
    ssize_t ret = send(sockfd, &train, sizeof(train.length) + train.length, 0);
    ERROR_CHECK(ret, -1, "send username for login");
    // 接收服务端发回的salt
    char salt[FULL_SALT_LEN] = {0};
    ssize_t n = recvn(sockfd, &train.length, sizeof(train.length));
    if (n <= 0) {
        perror("recv server salt length");
        close(sockfd);
        exit(1);
    }
    n = recvn(sockfd, salt, train.length);
    if (n <= 0) {
        perror("recv server salt");
        close(sockfd);
        exit(1);
    }
    salt[n] = '\0';
    printf("Received salt: %s\n", salt);
    // 提示用户输入密码，并用salt加密
    char *password = getpass("Please enter your password: ");
    if (password == NULL) {
        perror("getpass");
        close(sockfd);
        exit(1);
    }
    char *encrypted = crypt(password, salt);
    if (encrypted == NULL) {
        perror("crypt");
        close(sockfd);
        exit(1);
    }
    printf("encrypted: %s\n",encrypted);
    // 发送加密后的密码给服务器
    train.length = strlen(encrypted);
    printf("encrypte length: %d\n",train.length);
    memcpy(train.data, encrypted, train.length);
    train.data[train.length]='\0';
    printf("train.data: %s\n",train.data);
    ret = send(sockfd, &train, sizeof(train.length) + train.length, 0);
    ERROR_CHECK(ret, -1, "send encrypted password");
    // 接收登录结果
    char response[BUF_SIZE] = {0};
    n = recvn(sockfd, &train.length, sizeof(train.length));
    if (n <= 0) {
        perror("recv response length");
        close(sockfd);
        exit(1);
    }
    n = recvn(sockfd, response, train.length);
    if (n <= 0) {
        perror("recv response");
        close(sockfd);
        exit(1);
    }

    response[n] = '\0';
    printf("login response: %s", response);
    // 登录成功
    if (strstr(response, "Successful") != NULL) {
        printf("Entering main interface...\n");
    } else {
        // 登录失败
        close(sockfd);
        exit(1);
    }
    return 0;
}

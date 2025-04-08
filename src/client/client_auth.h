#ifndef CLIENT_AUTH_H
#define CLIENT_AUTH_H

#include "client_file_transfer.h"
#include <cloudisk.h>

#define FULL_SALT_LEN 32 // 完整盐值长度

// 客户端注册
int client_register(int sockfd, char *mode, char *username, char *passsword);
// 客户端登录
int client_login(int sockfd, char *username);

#endif

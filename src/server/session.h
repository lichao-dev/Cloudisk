#ifndef SESSION_H
#define SESSION_H

#include "auth.h"
#include "config.h"
#include "db.h"
#include "server_init.h"
#include "msg.h"
#include <cloudisk.h>
#include "file_db.h"

#define USERNAME_LEN 16 // 用户名最大长度
#define TOKEN_LEN 64    // 用户令牌长度
#define PASSWD_LEN 16   // 密码长度
// 表示用户数组状态，有连接或者无连接
typedef enum { DISCONNECTION, CONNECTION } UserState;

// 用户信息的结构体
typedef struct {
    int control_sockfd;          // 控制连接
    int data_sockfd;             // 数据连接
    char current_dir[PATH_SIZE];  // 当前目录
    char upload_dir[PATH_SIZE];   // 用户上传文件的存放路径
    UserState state;             // 用户状态，连接还是未连接
    char username[USERNAME_LEN]; // 用户名
    char token[TOKEN_LEN];       // 用于绑定控制和数据连接的令牌
} UserInfo;


// 处理控制连接
int handle_control_connection(UserInfo *user, int epfd, int control_sockfd, MYSQL *conn);
// 处理数据连接
int handle_data_connection(UserInfo *user, int data_sockfd);

#endif

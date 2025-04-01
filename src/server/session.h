#ifndef SESSION_H
#define SESSION_H

#include "config.h"
#include <cloudisk.h>
#include "server_init.h"

#define USERNAME_LEN 16 // 用户名最大长度
#define MSG_LEN 256     // 错误消息的数组长度
#define TOKEN_LEN 64    // 用户令牌长度
#define SALT_LEN 32     // 盐值长度
#define PASSWD_LEN 16   // 密码长度

// 表示用户数组状态，有连接或者无连接
typedef enum { DISCONNECTION, CONNECTION } UserState;

// 用户信息的结构体
typedef struct {
    int control_sockfd;          // 控制连接
    int data_sockfd;             // 数据连接
    char current_dir[PATH_MAX];  // 当前目录
    char upload_dir[PATH_MAX];   // 用户上传文件的存放路径
    UserState state;             // 用户状态，连接还是未连接
    char username[USERNAME_LEN]; // 用户名
    char token[TOKEN_LEN];       // 用于绑定控制和数据连接的令牌
} UserInfo;

// 发送自定义命令反馈给客户端
int send_msg(int sockfd, const char *msg);
// 发送error的错误指示给客户端,prefix是消息前缀
int send_err_msg(int sockfd, const char *prefix);
// 处理控制连接
int handle_control_connection(UserInfo *user, int epfd, int control_sockfd);
// 处理数据连接
int handle_data_connection(UserInfo *user,int data_sockfd);

#endif

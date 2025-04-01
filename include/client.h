#ifndef CLIENT_H
#define CLIENT_H

#include "client_file_transfer.h"
#include <cloudisk.h>
#include "client_thread.h"

#define TOKEN_NUM 10  // 假设默认支持最多10个token
#define MAX_EVENTS 5  // 最多事件数
#define TOKEN_LEN 64 // 用于数据连接验证的token长度
#define DATA_PORT 9999 // 数据连接端口
#define SALT_LEN 32 // 盐值长度
#define PASSWD_LEN 16 // 密码长度

#endif

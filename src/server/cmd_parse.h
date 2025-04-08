#ifndef CMD_PARSE_H
#define CMD_PARSE_H

#include "commands.h"
#include "file_transfer.h"
#include "ls.h"
#include "msg.h"
#include "session.h"
#include "thread_pool.h"
#include <cloudisk.h>

#define CMD_LEN 256  // 命令长度
#define TOKEN_NUM 10 // 假设默认支持最多10个token

// 命令解析
int cmd_parse(UserInfo *user, char *cmd, MYSQL *conn);

#endif

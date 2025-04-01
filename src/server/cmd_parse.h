#ifndef CMD_PARSE_H
#define CMD_PARSE_H

#include <cloudisk.h>
#include "file_transfer.h"
#include "thread_pool.h"
#include "commands.h"
#include "ls_ll.h"
#include "session.h"

#define CMD_LEN 256  // 命令长度
#define TOKEN_NUM 10 // 假设默认支持最多10个token

// 命令解析
int cmd_parse(UserInfo *user, char *cmd);

#endif


#ifndef LS_H
#define LS_H

#include "session.h"
#include "commands.h"
#include "msg.h"
#include "db.h"

#define LIST_BUF_SIZE 8192 // 输出缓冲区大小
#define TERM_WIDTH 125 // 终端宽度

// 基于数据库的ls命令
int handle_ls_db(UserInfo *user, MYSQL *conn);

#endif

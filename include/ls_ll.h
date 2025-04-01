#ifndef LS_LL_H
#define LS_LL_H

#include "session.h"
#include "commands.h"

#define LIST_BUF_SIZE 8192

// 处理ls命令
int handle_ls(UserInfo *user, char *dir);
// 处理ll命令
int handle_ll(UserInfo *user, char *dir);

#endif

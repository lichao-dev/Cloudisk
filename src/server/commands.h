#ifndef COMMANDS_H
#define COMMANDS_H

#include "session.h"
#include <cloudisk.h>

// 封装获取家目录的函数
char *get_home_directory();
// 处理cd命令
int handle_cd(UserInfo *user, char *dir);
// 处理pwd命令的函数
int handle_pwd(UserInfo *user);
// mkdir
int handle_mkdir(UserInfo *user, char *dir);
// rmdir
int handle_rmdir(UserInfo *user, char *dir);
// rm
int handle_rm(UserInfo *user, char *file);

#endif

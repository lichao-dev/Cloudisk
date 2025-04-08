#ifndef COMMANDS_H
#define COMMANDS_H

#include "auth.h"
#include "file_db.h"
#include "msg.h"
#include "session.h"
#include <cloudisk.h>

// 处理cd命令
int handle_cd_db(UserInfo *user, char *dir, MYSQL *conn);
// 处理pwd命令的函数
int handle_pwd(UserInfo *user);
// mkdir
int handle_mkdir_db(UserInfo *user, char *dir, MYSQL *conn);
// rmdir
int handle_rmdir_db(UserInfo *user, char *dir, MYSQL *conn);
// rm
int handle_rm_db(UserInfo *user, char *file, MYSQL *conn);

#endif

#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H

#include "session.h"
#include <cloudisk.h>
#include "commands.h"
#include "msg.h"
#include "file_db.h"
#include "auth.h"

#define LARGE_FILE_THRESHOLD 100 * 1024 * 1024 // 大文件阈值(100MB),超过便认为是大文件
#define BUF_SIZE 4096

typedef struct {
    UserInfo user;
    MYSQL *conn;
    char filename[NAME_MAX + 1];
} FileTransferArg;

// 文件下载
void handle_gets(void *arg);
// 文件上传
void handle_puts(void *arg);

#endif

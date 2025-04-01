#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H

#include "session.h"
#include <cloudisk.h>
#include "commands.h"

#define LARGE_FILE_THRESHOLD 100 * 1024 * 1024 // 大文件阈值(100MB),超过便认为是大文件
#define DATA_SIZE 4096
#define SHA256_STR_LEN 64   // SHA-256 十六进制字符串长度
#define BUF_SIZE 4096

typedef struct {
    int length;  // 火车头，存储数据长度
    char data[DATA_SIZE]; // 数据
} Train;

typedef struct {
    UserInfo user;
    char filename[NAME_MAX + 1];
} FileTransferArg;

// 文件下载
void handle_gets(void *arg);
// 文件上传
void handle_puts(void *arg);

#endif

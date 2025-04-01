#ifndef CLIENT_FILE_TRANSFER_H
#define CLIENT_FILE_TRANSFER_H

#include <cloudisk.h>

#define LARGE_FILE_THRESHOLD 100 * 1024 * 1024 // 大文件阈值(100MB),超过便认为是大文件
#define DATA_SIZE 4096
#define SHA256_STR_LEN 64 // sha1为64字节的十六进制字符串
#define BUF_SIZE 4096

typedef struct {
    int length;           // 火车头，存储数据长度
    char data[DATA_SIZE]; // 数据
} Train;
// 表示传输类型
typedef enum{
    GETS,
    PUTS
}TransferType;
// 封装文件传输任务的参数结构体
typedef struct {
    int sockfd;
    char filename[NAME_MAX+1];
    TransferType type;
} FileTransferThreadArg;

// 处理下载文件
int client_gets(int sockfd, char *filename);
// 处理上传文件
int client_puts(int sockfd, char *filename);

#endif

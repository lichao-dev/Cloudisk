#include "client_thread.h"

// 线程处理函数(用于调用client_gets/client_puts)
void *transfer_thread(void *arg) {
    FileTransferThreadArg *ftarg = (FileTransferThreadArg *)arg;
    if (ftarg->type == GETS) {
        client_gets(ftarg->sockfd, ftarg->filename);
    } else if (ftarg->type == PUTS) {
        client_puts(ftarg->sockfd, ftarg->filename);
    }
    free(ftarg);
    return NULL;
}
// 封装创建传输线程的函数
int start_transfer_thread(int sockfd, char **tokens, int count) {
    FileTransferThreadArg *ftarg = NULL;
    pthread_t tid;

    // 如果是gets命令
    if (strcmp(tokens[0], "gets") == 0) {
        if (count < 2) { // 判断参数个数
            fprintf(stderr, "ERROR: gets requires a filename\n");
            return -1;
        }
        ftarg = (FileTransferThreadArg *)malloc(sizeof(FileTransferThreadArg));
        if (ftarg == NULL) {
            perror("malloc");
            return -1;
        }
        ftarg->sockfd = sockfd;
        strncpy(ftarg->filename, tokens[1], NAME_MAX);
        ftarg->filename[NAME_MAX] = '\0';
        ftarg->type = GETS; // gets命令
    } else if (strcmp(tokens[0], "puts") == 0) {
        // 如果是puts命令
        if (count < 2) { // 判断参数个数
            fprintf(stderr, "ERROR: puts requires a filename\n");
            return -1;
        }
        ftarg = (FileTransferThreadArg *)malloc(sizeof(FileTransferThreadArg));
        if (ftarg == NULL) {
            perror("malloc");
            return -1;
        }
        ftarg->sockfd = sockfd;
        strncpy(ftarg->filename, tokens[1], NAME_MAX);
        ftarg->filename[NAME_MAX] = '\0';
        ftarg->type = PUTS; // puts命令
    } else {
        // 非传输命令，不处理
        return 0;
    }

    // 创建子线程去处理文件传输
    if (pthread_create(&tid, NULL, transfer_thread, ftarg) != 0) {
        perror("pthread_create");
        free(ftarg);
        return -1;
    } else {
        pthread_detach(tid); // 分离线程
    }
    return 0;
}

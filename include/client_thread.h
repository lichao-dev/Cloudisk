#ifndef CLIENT_THREAD_H

#include <cloudisk.h>
#include "client_file_transfer.h"
// 封装创建传输线程的函数
int start_transfer_thread(int sockfd, char **tokens, int count);

#endif

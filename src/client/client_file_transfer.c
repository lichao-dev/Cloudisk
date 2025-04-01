#include "client_file_transfer.h"

// 计算指定文件从offset开始length长度的数据的sha256值
static int calculate_file_sha256_evp(char *filename, off_t offset, off_t length, char *sha256_str) {
    // 打开文件
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        return -1;
    }
    // 定位到offset
    if (lseek(fd, offset, SEEK_SET) < 0) {
        close(fd);
        return -1;
    }

    EVP_MD_CTX *sha256_ctx = EVP_MD_CTX_new();
    if (sha256_ctx == NULL) {
        close(fd);
        return -1;
    }
    // 初始化， 1成功，0失败
    if (EVP_DigestInit_ex(sha256_ctx, EVP_sha256(), NULL) != 1) {
        EVP_MD_CTX_free(sha256_ctx);
        close(fd);
        return -1;
    }

    char buf[BUF_SIZE] = {0};
    off_t total = 0; // 总共读取的字节数
    ssize_t n;       // read的字节数
    ssize_t to_read; // 要read的字节数
    // 循环读取文件直到length
    while (total < length) {
        to_read = (length - total) > BUF_SIZE ? BUF_SIZE : (length - total);
        n = read(fd, buf, to_read);
        if (n == -1) {
            // 释放
            EVP_MD_CTX_free(sha256_ctx);
            close(fd);
            return -1;
        }
        // 更新evp上下文，将读取的数据加入计算
        if (EVP_DigestUpdate(sha256_ctx, buf, n) != 1) {
            EVP_MD_CTX_free(sha256_ctx);
            close(fd);
            return -1;
        }
        total += n;
    }

    // 定义一个数组存放最终的二进制散列值，sha256的输出长度为32字节
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0; // 存放实际得到的digest长度
    // 完成散列计算，将结果跑存入digest数组
    // 成功1，失败0
    if (EVP_DigestFinal_ex(sha256_ctx, digest, &digest_len) != 1) {
        EVP_MD_CTX_free(sha256_ctx);
        close(fd);
        return -1;
    }

    // 释放evp上下文
    EVP_MD_CTX_free(sha256_ctx);
    // 关闭文件
    close(fd);

    // 将二进制散列值转换为64位十六进制字符串
    // 每个字节用两个十六进制表示
    for (unsigned int i = 0; i < digest_len; ++i) {
        sprintf(&sha256_str[i * 2], "%02x", digest[i]);
    }
    // 添加字符串结束符
    sha256_str[digest_len * 2] = '\0';

    return 0;
}

// 打印进度条
static void print_progress(off_t current, off_t total) {
    // 用已接收的字节数除以文件总大小并乘以100来计算当前的传输百分比
    // 结果转换为浮点数，表示已经完成的百分比
    double percent = ((double)current * 100.0) / total;
    // 表示进度条宽度，可以根据终端大小调整
    int bar_width = 50;
    // 打印进度条前缀
    // \r表示回到当前行的开头，从而可以覆盖之前输出的内容
    printf("\rProgress: [");
    // 计算进度条内“已完成”部分的长度
    // 是为了计算应该在总宽度里填充多少个字符表示“已完成”部分
    int pos = (int)((percent * bar_width) / 100.0);
    // 构造进度条，这里会遍历进度条的每一个字符位置
    for (int i = 0; i < bar_width; i++) {
        if (i < pos) {
            printf("="); // 当i小于pos，说明这部分已经完成，用=显示
        } else if (i == pos) {
            printf(">"); // 当i恰好等于pos时，打印一个箭头
        } else {
            printf(" "); // 其余位置打印空格，表示未完成
        }
    }
    // 打印进度条后缀和百分比
    printf("] %.2f%%", percent);
    fflush(stdout); // 刷新缓冲区，确保进度条立即显示
    // 传输完成时，换行
    if (current >= total) {
        printf("\n");
    }
}
// 循环接收n字节
static int recvn(int sockfd, void *buf, ssize_t n) {
    ssize_t sret;
    char *p = (char *)buf; // void *不能做偏移
    ssize_t cursize = 0;
    while (cursize < n) {
        sret = recv(sockfd, p + cursize, n - cursize, 0);
        if (sret <= 0) {
            return sret;
        }
        cursize += sret;
    }
    return cursize;
}

// 处理下载文件
int client_gets(int sockfd, char *filename) {
    Train train;
    off_t resume_offset = 0; // 继续传输位置
    // 检查是否已存在目标文件
    int fd_local = open(filename, O_RDWR);
    if (fd_local != -1) {
        // 进入这里说明文件存在
        struct stat st;
        if (fstat(fd_local, &st) == 0) {
            // 得到目前文件大小，也就是已下载的字节数，作为续传断点
            resume_offset = st.st_size;
        }
        close(fd_local);
    }
    // 将断点信息（resume_offset）发送给服务端
    train.length = sizeof(resume_offset);
    memcpy(train.data, &resume_offset, train.length);
    if (send(sockfd, &train, sizeof(train.length) + train.length, 0) == -1) {
        perror("send resume_offset");
        return -1;
    }

    // 如果断点大于0，说明之前下载过一部分数据
    // 那么就计算这部分数据的sha256值，并发送给服务端用于校验
    char sha256_local[SHA256_STR_LEN + 1] = {0}; // 本地文件的sha256值
    if (resume_offset > 0) {
        if (calculate_file_sha256_evp(filename, 0, resume_offset, sha256_local) != 0) {
            perror("calculate sha256");
            return -1;
        }
        // 发送sha256字符串
        train.length = strlen(sha256_local) + 1;
        memcpy(train.data, sha256_local, train.length);
        if (send(sockfd, &train, sizeof(train.length) + train.length, 0) == -1) {
            perror("send sha256_local");
            return -1;
        }
    }

    // 接收文件名长度
    ssize_t ret = recvn(sockfd, &train.length, sizeof(train.length));
    if (ret == -1) {
        perror("recvn file name length");
        return -1;
    } else if (ret == 0) {
        printf("peer disconnected\n");
        return -1;
    }
    // 接收文件名
    ret = recvn(sockfd, train.data, train.length);
    if (ret == -1) {
        perror("recvn file name");
        return -1;
    } else if (ret == 0) {
        printf("peer disconnected\n");
        return -1;
    }
    train.data[train.length - 1] = '\0';
    // 检查文件名是否和请求的文件名相同
    if (strcmp(filename, train.data) != 0) {
        printf("recvn file name is not correct\n");
        return -1;
    }
    // 接收剩余文件大小(remaining-size=总大小-resume-offset)
    ret = recvn(sockfd, &train.length, sizeof(train.length));
    if (ret == -1) {
        perror("recvn remaining file size length");
        return -1;
    } else if (ret == 0) {
        printf("peer disconnected\n");
        return -1;
    }
    off_t remaining_size;
    ret = recvn(sockfd, &remaining_size, train.length);
    if (ret == -1) {
        perror("recvn remaining file size");
        return -1;
    } else if (ret == 0) {
        printf("peer disconnected\n");
        return -1;
    }
    // 再接收文件总大小
    ret = recvn(sockfd, &train.length, sizeof(train.length));
    if (ret == -1) {
        perror("recvn total file size length");
        return -1;
    } else if (ret == 0) {
        printf("peer disconnected\n");
        return -1;
    }
    off_t total_size;
    ret = recvn(sockfd, &total_size, train.length);
    if (ret == -1) {
        perror("recvn total file size");
        return -1;
    } else if (ret == 0) {
        printf("peer disconnected\n");
        return -1;
    }
    // 本地已存在完整文件且被服务端验证通过
    if (remaining_size == 0) {
        printf("File already exists and verified\n");
        return 0;
    }
    // 根据总大小和剩余大小是否相等决定采用续传还是重新下载
    int fd;
    // 如果剩余文件大小不等于总大小(说明存在的部分验证通过)
    // 如果剩余文件大小等于0，则说明文件已经存在且验证通过
    // 客户端收到的remaining_size=0，是因为在服务端检查后知道客户端没有该文件
    // 或者就是该文件存在一部分但是不匹配，或者是该文件完全存在
    if (total_size != remaining_size && remaining_size != 0) {
        // 续传
        fd = open(filename, O_RDWR);
        if (fd == -1) {
            perror("open file to resume download");
            return -1;
        }
        printf("Resuming download from offset %ld, local SHA-256: %s\n", resume_offset,
               sha256_local);

    } else {
        // 走到这里说明文件不存在或者就是文件已存在或者存在一部分但是不匹配
        resume_offset = 0;
        fd = open(filename, O_CREAT | O_RDWR | O_TRUNC, 0666);
        if (fd == -1) {
            perror("create file");
            return -1;
        }
    }

    off_t to_recv_size = remaining_size + resume_offset; // 将要接受的文件的大小
    // 根据要接收文件大小分情况处理
    if (to_recv_size > LARGE_FILE_THRESHOLD) {
        // 大文件，使用ftruncate预分配空间+mmap写入
        if (ftruncate(fd, to_recv_size) == -1) {
            perror("ftruncate");
            close(fd);
            return -1;
        }

        // 映射文件到内存
        char *p = (char *)mmap(NULL, to_recv_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) {
            perror("mmap");
            close(fd);
            return -1;
        }

        off_t bytes_received = resume_offset; // 已收到的文件字节数
        while (bytes_received < to_recv_size) {
            ret = recvn(sockfd, p + bytes_received, to_recv_size - bytes_received);
            if (ret == -1) {
                perror("recvn file");
                close(fd);
                munmap(p, to_recv_size);
                return -1;
            } else if (ret == 0) {
                printf("peer disconnected\n");
                munmap(p, to_recv_size);
                close(fd);
                return -1;
            }
            bytes_received += ret;
            // 更新进度条
            print_progress(bytes_received, to_recv_size);
        }

        // 解除映射
        munmap(p, to_recv_size);
    } else {
        // 小文件,先将文件指针移动到续传位置
        if (lseek(fd, resume_offset, SEEK_SET) == -1) {
            perror("lseek");
            close(fd);
            return -1;
        }
        off_t offset = resume_offset;
        while (1) {
            // 收车头
            ret = recvn(sockfd, &train.length, sizeof(train.length));
            if (ret == -1) {
                perror("recvn data length");
                close(fd);
                return -1;
            } else if (ret == 0) {
                printf("Peer disconnected\n");
                close(fd);
                return -1;
            }
            if (train.length == 0) { // 传输结束
                break;
            }
            // 接受数据
            ret = recvn(sockfd, train.data, train.length);
            if (ret == -1) {
                perror("recvn file");
                close(fd);
                return -1;
            } else if (ret == 0) {
                printf("peer disconnected\n");
                close(fd);
                return -1;
            }

            // 写入文件
            if (write(fd, train.data, train.length) == -1) {
                perror("write file");
                close(fd);
                return -1;
            }
            offset += train.length;
            // 更新进度条
            print_progress(offset, to_recv_size);
        }
    }
    close(fd);
    // 下载结束，对整个文件进行校验，计算SHA-256，再和服务端发送的SHA-256比对
    char local_full_sha256[SHA256_STR_LEN + 1] = {0};
    if (calculate_file_sha256_evp(filename, 0, to_recv_size, local_full_sha256) != 0) {
        perror("calculate full sha256");
        return -1;
    }
    // 接受服务端的SHA-256
    char server_full_sha256[SHA256_STR_LEN + 1] = {0};
    ret = recvn(sockfd, &train.length, sizeof(train.length));
    if (ret <= 0) {
        if (ret == 0) {
            printf("peer closed data connection\n");
        } else {
            perror("recvn server_full_sha256 length");
        }
        return -1;
    }
    ret = recvn(sockfd, server_full_sha256, train.length);
    if (ret <= 0) {
        if (ret == 0) {
            printf("peer closed data connection\n");
        } else {
            perror("recvn server_full_sha256");
        }
        return -1;
    }
    server_full_sha256[train.length] = '\0';
    printf("Local full SHA256: %s\nServer full SHA256: %s\n", local_full_sha256,
           server_full_sha256);
    // 比较
    if (strcmp(local_full_sha256, server_full_sha256) != 0) {
        printf("Full file SHA-256 mismatch\n");
        return -1;
    }

    printf("OK: File downloaded and verified\n");
    return 0;
}

// 处理上传文件
int client_puts(int sockfd, char *filename) {
    Train train; // 申请小火车
    // 打开本地文件
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("open file");
        return -1;
    }
    // 获取文件大小
    struct stat st;
    if (fstat(fd, &st) == -1) {
        perror("fstat file");
        close(fd);
        return -1;
    }
    off_t file_size = st.st_size;

    // 发送文件名
    train.length = strlen(filename) + 1; // 包括末尾的'\0'
    memcpy(train.data, filename, train.length);
    ssize_t ret = send(sockfd, &train, sizeof(train.length) + train.length, 0);
    if (ret == -1) {
        perror("send file name");
        close(fd);
        return -1;
    }

    // 发送文件大小
    train.length = sizeof(file_size);
    memcpy(train.data, &file_size, train.length);
    ret = send(sockfd, &train, sizeof(train.length) + train.length, 0);
    if (ret == -1) {
        perror("send file size");
        close(fd);
        return -1;
    }

    // 发送文件内容
    if (file_size > LARGE_FILE_THRESHOLD) {
        // 大文件
        off_t total_sent = 0; // 总共发送的字节数
        while (total_sent < file_size) {
            ssize_t n = sendfile(sockfd, fd, NULL, file_size - total_sent);
            if (n == -1) {
                perror("sendfile");
                close(fd);
                return -1;
            }
            if (n == 0) {
                printf("sendfile returned 0 unexpectedly, no data transmitted\n");
                close(fd);
                return -1;
            }
            total_sent += n;
            // 更新进度条
            print_progress(total_sent, file_size);
        }
    } else {
        // 小文件
        off_t offset = 0;
        while (offset < file_size) {
            train.length = (file_size - offset) > DATA_SIZE ? DATA_SIZE : (file_size - offset);
            ssize_t bytes_read = read(fd, train.data, train.length);
            if (bytes_read == -1) {
                perror("read file");
                close(fd);
                return -1;
            }
            // 更新偏移量
            offset += bytes_read;
            // 发送数据
            ret = send(sockfd, &train, sizeof(train.length) + train.length, 0);
            if (ret == -1) {
                perror("send file");
                close(fd);
                return -1;
            }
            // 更新进度条
            print_progress(offset, file_size);
        }
        // 发送结束标志
        train.length = 0;
        ret = send(sockfd, &train.length, sizeof(train.length), 0);
        if (ret == -1) {
            perror("send end marker");
            close(fd);
            return -1;
        }
    }
    close(fd);
    printf("OK: File uploaded\n");
    return 0;
}

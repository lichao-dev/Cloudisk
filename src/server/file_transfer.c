#include "file_transfer.h"

// 计算指定文件从offset开始length长度的数据的sha256值
static int calculate_file_sha256_evp(int fd, off_t offset, off_t length, char *sha256_str) {
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

    // 将二进制散列值转换为64位十六进制字符串
    // 每个字节用两个十六进制表示
    for (unsigned int i = 0; i < digest_len; ++i) {
        sprintf(&sha256_str[i * 2], "%02x", digest[i]);
    }
    // 添加字符串结束符
    sha256_str[digest_len * 2] = '\0';

    return 0;
}

// 辅助函数：根据用户当前目录和请求的文件名构造完整的虚拟路径。
static void construct_virtual_path(UserInfo *user, const char *filename, char *virtual_path,
                                   size_t size) {
    if (strcmp(user->current_dir, "/") == 0) {
        snprintf(virtual_path, size, "/%s", filename);
    } else {
        snprintf(virtual_path, size, "%s/%s", user->current_dir, filename);
    }
}

// 文件下载
void handle_gets(void *arg) {

    FileTransferArg *transfer_arg = (FileTransferArg *)arg;
    UserInfo *user = &transfer_arg->user;
    MYSQL *conn = transfer_arg->conn;
    char *filename = transfer_arg->filename;

    // 构建虚拟文件路径
    char virtual_path[PATH_MAX] = {0};
    construct_virtual_path(user, filename, virtual_path, sizeof(virtual_path));

    // 获取用户id
    int user_id = get_user_id(conn, user->username);
    if (user_id < 0) {
        send_msg(user->control_sockfd, "ERROR: User not found in database\n");
        free(transfer_arg);
        return;
    }
    // 根据用户ID和虚拟路径查询数据库中的文件记录
    FileRecord record;
    if (file_db_get_record_by_path(conn, user_id, virtual_path, &record) != 0) {
        send_msg(user->control_sockfd, "ERROR: File not found in virtual file table\n");
        free(transfer_arg);
        return;
    }

    // 构造物理文件路径:upload_dir/sha256
    char file_path[PATH_MAX] = {0};
    snprintf(file_path, sizeof(file_path), "%s/%s", user->upload_dir, record.sha256);

    // 打开文件
    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        send_err_msg(user->control_sockfd, "open file");
        CLOUDISK_LOG_ERROR_CHECK("open file");
        free(transfer_arg); // 释放内存
        return;
    }

    // 获取文件大小
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd); // 关闭打开的文件描述符
        send_err_msg(user->control_sockfd, "fstat file");
        CLOUDISK_LOG_ERROR_CHECK("fstat file");
        free(transfer_arg); // 释放内存
        return;
    }
    off_t total_size = st.st_size; // 文件总大小

    Train train; // 申请一个小火车
    // 接收客户端发送的续传断点 resume_offset
    off_t resume_offset = 0;
    ssize_t ret = recvn(user->data_sockfd, &train.length, sizeof(train.length));
    if (ret <= 0) {
        if (ret == 0) {
            printf("peer closed data connection\n");
            CLOUDISK_LOG_INFO("%s closed data connection", user->username);
        } else {
            send_err_msg(user->control_sockfd, "recv resume_offset length");
            CLOUDISK_LOG_ERROR_CHECK("recv resume_offset length");
        }
        close(fd);
        free(transfer_arg);
        return;
    }
    ret = recvn(user->data_sockfd, &resume_offset, train.length);
    if (ret <= 0) {
        if (ret == 0) {
            printf("peer closed data connection\n");
            CLOUDISK_LOG_INFO("%s closed data connection", user->username);
        } else {
            send_err_msg(user->control_sockfd, "recv resume_offset");
            CLOUDISK_LOG_ERROR_CHECK("recv resume_offset");
        }
        close(fd);
        free(transfer_arg);
        return;
    }
    // 如果resume_offset大于0，则进行续传校验
    if (resume_offset > 0) {
        char client_sha256[SHA256_STR_LEN + 1] = {0};
        // 接收客户端发送来的已下载部分的SHA-256
        ret = recvn(user->data_sockfd, &train.length, sizeof(train.length));
        if (ret <= 0) {
            if (ret == 0) {
                printf("peer closed data connection\n");
                CLOUDISK_LOG_INFO("%s closed data connection", user->username);
            } else {
                send_err_msg(user->control_sockfd, "recv SHA-256 length");
                CLOUDISK_LOG_ERROR_CHECK("recv SHA-256 length");
            }
            close(fd);
            free(transfer_arg);
            return;
        }
        ret = recvn(user->data_sockfd, client_sha256, train.length);
        if (ret <= 0) {
            if (ret == 0) {
                printf("peer closed data connection\n");
                CLOUDISK_LOG_INFO("%s closed data connection", user->username);
            } else {
                send_err_msg(user->control_sockfd, "recv SHA-256");
                CLOUDISK_LOG_ERROR_CHECK("recv SHA-256");
            }
            close(fd);
            free(transfer_arg);
            return;
        }
        client_sha256[SHA256_STR_LEN] = '\0';
        char server_sha256[SHA256_STR_LEN + 1] = {0};
        // 计算前resume_offset字节的SHA-256
        if (calculate_file_sha256_evp(fd, 0, resume_offset, server_sha256) != 0) {
            perror("calculate sha256");
            close(fd);
            free(transfer_arg);
            return;
        }
        // 比较客户端与服务端的SHA-256，不一致则不续传，发送整个文件
        if (strcmp(client_sha256, server_sha256) != 0) {
            send_msg(user->control_sockfd,
                     "SHA-256 mismatch on resume portion, the entire file will be transferred");
            resume_offset = 0; // 从头开始传输
        }
        // 将文件指针定位到续传位置
        if (lseek(fd, resume_offset, SEEK_SET) < 0) {
            perror("lseek resume_offset");
            close(fd);
            free(transfer_arg);
            return;
        }
    }

    // 先发送文件名
    train.length = strlen(filename) + 1; // 包括末尾的\0
    // 填充文件名到车厢
    memcpy(train.data, filename, train.length);
    // 发送文件名
    ret = send(user->data_sockfd, &train, sizeof(train.length) + train.length, 0);
    if (ret < 0) {
        close(fd);
        send_err_msg(user->control_sockfd, "ERROR: Send file name");
        CLOUDISK_LOG_ERROR_CHECK("Send file name");
        free(transfer_arg);
        return;
    }

    // 计算剩余需要传输的数据大小，并发送给客户端
    off_t remaining_size = total_size - resume_offset;
    train.length = sizeof(remaining_size);
    memcpy(train.data, &remaining_size, train.length);
    ret = send(user->data_sockfd, &train, sizeof(train.length) + train.length, 0);
    if (ret < 0) {
        close(fd);
        send_err_msg(user->control_sockfd, "ERROR: Send remaining file size");
        CLOUDISK_LOG_ERROR_CHECK("Send remaining file size");
        free(transfer_arg);
        return;
    }
    // 服务端还要把总大小也发给客户端，客户端发现剩余大小和总大小相等则接收整个文件
    train.length = sizeof(total_size);
    memcpy(train.data, &total_size, train.length);
    ret = send(user->data_sockfd, &train, sizeof(train.length) + train.length, 0);
    if (ret < 0) {
        close(fd);
        send_err_msg(user->control_sockfd, "ERROR: Send total file size");
        CLOUDISK_LOG_ERROR_CHECK("Send total file size");
        free(transfer_arg);
        return;
    }
    // remaining_size == 0说明客户端存在整个文件且已验证，因此不需要下载，直接返回
    if (remaining_size == 0) {
        close(fd);
        free(transfer_arg);
        return;
    }
    // 分大文件和小文件两种情况传输
    if (remaining_size > LARGE_FILE_THRESHOLD) {
        // 大文件
        off_t total_sent = 0;
        while (total_sent < remaining_size) {
            ssize_t n = sendfile(user->data_sockfd, fd, NULL, remaining_size - total_sent);
            if (n < 0) {
                perror("sendfile");
                close(fd);
                send_err_msg(user->control_sockfd, "ERROR: Send file content");
                CLOUDISK_LOG_ERROR_CHECK("Send file content");
                free(transfer_arg);
                return;
            }
            if (n == 0) {
                printf("sendfile returned 0 unexpectedly, no data transmitted\n");
                close(fd);
                free(transfer_arg);
                return;
            }
            total_sent += n;
        }
    } else {
        // 小文件
        off_t offset = 0; // 偏移量，也是已发送字节数
        while (offset < remaining_size) {
            train.length =
                (remaining_size - offset > DATA_SIZE) ? DATA_SIZE : (remaining_size - offset);
            // 读取文件内容
            ssize_t bytes_read = read(fd, train.data, train.length);
            if (bytes_read < 0) {
                close(fd);
                send_err_msg(user->control_sockfd, "ERROR: read file");
                CLOUDISK_LOG_ERROR_CHECK("read file");
                free(transfer_arg);
                return;
            }
            // 更新偏移量
            offset += bytes_read;
            // 发送数据
            ssize_t bytes_sent =
                send(user->data_sockfd, &train, sizeof(train.length) + train.length, 0);
            if (bytes_sent < 0) {
                close(fd);
                send_err_msg(user->control_sockfd, "ERROR: send file");
                CLOUDISK_LOG_ERROR_CHECK("send file");
                free(transfer_arg);
                return;
            }
        }
        // 发送结束标志
        train.length = 0;
        ret = send(user->data_sockfd, &train.length, sizeof(train.length), 0);
        if (ret < 0) {
            close(fd);
            send_err_msg(user->control_sockfd, "ERROR: send end marker");
            CLOUDISK_LOG_ERROR_CHECK("send end marker");
            free(transfer_arg);
            return;
        }
    }

    close(fd);
    // 传输完成后，重新打开文件计算全文件 SHA-256，并发送给客户端用于最终比对
    fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        perror("Failed to reopen the file");
        free(transfer_arg);
        return;
    }
    char full_sha256[SHA256_STR_LEN + 1] = {0};
    if (calculate_file_sha256_evp(fd, 0, total_size, full_sha256) != 0) {
        perror("calculate full file sha256");
        close(fd);
        free(transfer_arg);
        return;
    }
    close(fd);
    // 发送
    train.length = strlen(full_sha256) + 1;
    memcpy(train.data, full_sha256, train.length);
    if (send(user->data_sockfd, &train, sizeof(train.length) + train.length, 0) < 0) {
        perror("send full SHA-256");
        free(transfer_arg);
        return;
    }
    free(transfer_arg);
    CLOUDISK_LOG_INFO("%s: File %s downloaded successfully", user->username, filename);
}

// 文件上传
void handle_puts(void *arg) {
    FileTransferArg *transfer_arg = (FileTransferArg *)arg;
    UserInfo *user = &transfer_arg->user;
    MYSQL *conn = transfer_arg->conn;
    char *filename = transfer_arg->filename;

    printf("Handling PUTS %s for user %s\n", filename, user->username);

    Train train;
    // 接收文件名
    ssize_t ret = recvn(user->data_sockfd, &train.length, sizeof(train.length));
    if (ret <= 0) {
        if (ret == 0) {
            printf("peer closed data connection\n");
            CLOUDISK_LOG_INFO("%s closed data connection", user->username);
        } else {
            send_err_msg(user->control_sockfd, "recv file name length");
            CLOUDISK_LOG_ERROR_CHECK("recv file name length");
        }
        free(transfer_arg);
        return;
    }
    ret = recvn(user->data_sockfd, train.data, train.length);
    if (ret <= 0) {
        if (ret == 0) {
            printf("peer closed data connection\n");
            CLOUDISK_LOG_INFO("%s closed data connection", user->username);
        } else {
            send_err_msg(user->control_sockfd, "recv file name");
            CLOUDISK_LOG_ERROR_CHECK("recv file name");
        }
        free(transfer_arg);
        return;
    }
    train.data[train.length - 1] = '\0';
    printf("FILE NAME: %s\n",train.data);
    // 检查文件名是否匹配
    if (strcmp(train.data, filename) != 0) {
        send_msg(user->control_sockfd, "file name mismatch\n");
        CLOUDISK_LOG_INFO("%s: file name mismatch", user->username);
        free(transfer_arg);
        return;
    }

    // 接收文件大小
    ret = recvn(user->data_sockfd, &train.length, sizeof(train.length));
    if (ret <= 0) {
        if (ret == 0) {
            printf("peer closed data connection\n");
            CLOUDISK_LOG_INFO("%s closed data connection", user->username);
        } else {
            send_err_msg(user->control_sockfd, "recv file size length");
            CLOUDISK_LOG_ERROR_CHECK("recv file size length");
        }
        free(transfer_arg);
        return;
    }
    off_t file_size;
    ret = recvn(user->data_sockfd, &file_size, train.length);
    if (ret <= 0) {
        if (ret == 0) {
            printf("peer closed data connection\n");
            CLOUDISK_LOG_INFO("%s closed data connection", user->username);
        } else {
            send_err_msg(user->control_sockfd, "recv file size");
            CLOUDISK_LOG_ERROR_CHECK("recv file size");
        }
        free(transfer_arg);
        return;
    }
    printf("FILE SIZE = %ld\n",file_size);
    // 接收客户端发送的文件全局SHA-256值
    ret = recvn(user->data_sockfd, &train.length, sizeof(train.length));
    if (ret <= 0) {
        if (ret == 0) {
            printf("peer closed data connection\n");
            CLOUDISK_LOG_INFO("%s closed data connection", user->username);
        } else {
            send_err_msg(user->control_sockfd, "recv SHA-256 length");
            CLOUDISK_LOG_ERROR_CHECK("recv SHA-256 length");
        }
        free(transfer_arg);
        return;
    }
    char client_hash[SHA256_STR_LEN + 1] = {0};
    ret = recvn(user->data_sockfd, client_hash, train.length);
    if (ret <= 0) {
        if (ret == 0) {
            printf("peer closed data connection\n");
            CLOUDISK_LOG_INFO("%s closed data connection", user->username);
        } else {
            send_err_msg(user->control_sockfd, "recv SHA-256");
            CLOUDISK_LOG_ERROR_CHECK("recv SHA-256");
        }
        free(transfer_arg);
        return;
    }
    client_hash[train.length] = '\0';
    printf("RECV CLIENT HASH: %s\n",client_hash);
    // 获取用户id
    int user_id = get_user_id(conn, user->username);
    printf("user id at line 450 : %d\n",user_id);
    if (user_id < 0) {
        send_msg(user->control_sockfd, "ERROR: User not found in database\n");
        free(transfer_arg);
        return;
    }
    printf("user id at line 455 : %d\n",user_id);
    // 获取当前目录的id，作为上传文件的父目录id
    int parent_id = get_dir_id(conn, user_id, user->current_dir);
    if (parent_id < 0) {
        // 当前目录没有记录
        parent_id = 0;
    }
    printf("[handle_puts] user_id = %d, parent_id = %d\n",user_id,parent_id);

    // 秒传检测，查询是否已存在相同hash的文件记录
    FileRecord existing;
    if (file_db_find_by_hash(conn, client_hash, &existing) == 0) {
        // 已存在，只在虚拟文件表插入一条记录
        FileRecord new_record;
        memset(&new_record, 0, sizeof(new_record));
        new_record.user_id = user_id;
        new_record.parent_id = parent_id;
        strncpy(new_record.filename, filename, FILENAME_SIZE - 1);
        snprintf(new_record.path, sizeof(new_record.path), "%s/%s", user->current_dir, filename);
        new_record.type = 'f';
        new_record.filesize = file_size;
        strncpy(new_record.sha256, client_hash, SHA256_STR_LEN);
        if (file_db_create_record(conn, &new_record) != 0) {
            send_msg(user->control_sockfd, "ERROR: Insert file record failed\n");
            free(transfer_arg);
            return;
        }
        send_msg(user->control_sockfd, "OK: File uploaded (instant transfer)\n");
        free(transfer_arg);
        return;
    }

    // 文件不存在，需完整上传，构造物理文件路径：upload_dir/sha256
    char file_path[PATH_MAX] = {0};
    snprintf(file_path, sizeof(file_path), "%s/%s", user->upload_dir, client_hash);
    printf("[handle_puts] file path: %s\n",file_path);
    // 判断该用户的上传目录是否存在，不存在则创建
    struct stat st;
    if (stat(user->upload_dir, &st) == -1) {
        if (errno == ENOENT) {
            if (mkdir(user->upload_dir, 0755) == -1) {
                send_err_msg(user->control_sockfd, "create upload directory");
                CLOUDISK_LOG_ERROR_CHECK("create upload directory");
                return;
            }
            send_msg(user->control_sockfd, "\nupload directory created\n");
            CLOUDISK_LOG_INFO("%s: upload directory created", user->username);
        } else {
            send_err_msg(user->control_sockfd, "stat upload directory");
            CLOUDISK_LOG_ERROR_CHECK("stat upload directory");
            return;
        }
    }
    // 创建或打开文件
    int fd = open(file_path, O_CREAT | O_RDWR | O_TRUNC, 0666);
    if (fd == -1) {
        send_err_msg(user->control_sockfd, "open/create file");
        CLOUDISK_LOG_ERROR_CHECK("open/create file");
        free(transfer_arg);
        return;
    }

    // 根据文件大小分情况处理
    if (file_size > LARGE_FILE_THRESHOLD) {
        printf("BIG FILE\n");
        // 大文件，使用ftruncate+mmap写入
        if (ftruncate(fd, file_size) == -1) {
            send_err_msg(user->control_sockfd, "ftruncate");
            CLOUDISK_LOG_ERROR_CHECK("ftruncate");
            close(fd);
            free(transfer_arg);
            return;
        }
        // 映射文件
        char *p = (char *)mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (p == MAP_FAILED) {
            send_err_msg(user->control_sockfd, "mmap");
            CLOUDISK_LOG_ERROR_CHECK("mmap");
            close(fd);
            free(transfer_arg);
            return;
        }
        off_t bytes_received = 0; // 已收到的文件字节数
        while (bytes_received < file_size) {
            ret = recvn(user->data_sockfd, p + bytes_received, file_size - bytes_received);
            if (ret <= 0) {
                if (ret == 0) {
                    printf("peer closed data connection\n");
                    CLOUDISK_LOG_INFO("%s closed data connection", user->username);
                } else {
                    send_err_msg(user->control_sockfd, "recv file");
                    CLOUDISK_LOG_ERROR_CHECK("recv file");
                }
                munmap(p, file_size);
                close(fd);
                free(transfer_arg);
                return;
            }
            bytes_received += ret;
        }
        munmap(p, file_size);
    } else {
        printf("SMALL FILE\n");
        // 小文件,循环直到收到结束标志
        while (1) {
            // 接收数据长度
            ret = recvn(user->data_sockfd, &train.length, sizeof(train.length));
            if (ret <= 0) {
                if (ret == 0) {
                    printf("peer closed data connection\n");
                    CLOUDISK_LOG_INFO("%s closed data connection", user->username);
                } else {
                    send_err_msg(user->control_sockfd, "recv data length");
                    CLOUDISK_LOG_ERROR_CHECK("recv data length");
                }
                close(fd);
                free(transfer_arg);
                return;
            }

            if (train.length == 0) { // 结束标志
                break;
            }
            // 接收数据
            ret = recvn(user->data_sockfd, train.data, train.length);
            if (ret <= 0) {
                if (ret == 0) {
                    printf("peer closed data connection\n");
                    CLOUDISK_LOG_INFO("%s closed data connection", user->username);
                } else {
                    send_err_msg(user->control_sockfd, "recv data");
                    CLOUDISK_LOG_ERROR_CHECK("recv data");
                }
                close(fd);
                free(transfer_arg);
                return;
            }
            // 写入文件
            if (write(fd, train.data, train.length) == -1) {
                send_err_msg(user->control_sockfd, "write file");
                CLOUDISK_LOG_ERROR_CHECK("write file");
                close(fd);
                free(transfer_arg);
                return;
            }
        }
    }
    close(fd);
    // 上传结束后，在虚拟文件表中插入记录
    FileRecord new_record;
    memset(&new_record, 0, sizeof(new_record));
    new_record.user_id = user_id;
    new_record.parent_id = parent_id;
    strncpy(new_record.filename, filename, FILENAME_SIZE - 1);
    snprintf(new_record.path, sizeof(new_record.path), "%s/%s", user->current_dir, filename);
    new_record.type = 'f';
    new_record.filesize = file_size;
    strncpy(new_record.sha256, client_hash, SHA256_STR_LEN);
    if (file_db_create_record(conn, &new_record) != 0) {
        send_msg(user->control_sockfd, "ERROR: Insert file record failed\n");
        free(transfer_arg);
        return;
    }
    free(transfer_arg);
}

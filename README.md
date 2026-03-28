## 概述

Cloudisk 是一个基于 C/C++ 开发的云存储系统，实现了高效的文件秒传和断点续传功能。本文档详细介绍这两项核心技术的实现原理。

## 1. 文件秒传原理

### 1.1 基本概念

文件秒传（Instant Transfer）是一种基于文件内容去重的技术，通过计算文件的哈希值来判断服务器是否已存储相同内容的文件，如果存在则无需重复上传，直接完成"传输"。

### 1.2 技术实现

#### 1.2.1 核心数据结构

```c
// 文件记录结构
typedef struct {
    int id;                       // 文件记录ID
    char filename[FILENAME_SIZE]; // 用户上传的原始文件名
    int user_id;                  // 所属用户ID
    int parent_id;                // 父目录ID(0表示根目录)
    char path[PATH_MAX];         // 完整虚拟路径
    char type;                    // 'f'表示文件，'d'表示目录
    char sha256[SHA256_SIZE];     // 文件内容的SHA-256值
    long filesize;                // 文件大小
} FileRecord;
```

#### 1.2.2 物理存储策略

- **存储路径**：`upload_dir/[SHA256]`
- **去重机制**：使用 SHA256 作为物理文件名，相同内容的文件只存储一份
- **虚拟文件系统**：每个用户维护独立的虚拟文件视图

#### 1.2.3 秒传流程

1. **客户端计算文件哈希**
```c
// 计算整个文件的 SHA256 值（作为全局哈希，用于秒传检测）
char file_hash[SHA256_STR_LEN+1]={0};
if(calculate_file_sha256_evp(filename,0,file_size,file_hash)!=0){
    perror("calculate sha256");
    close(fd);
    return -1;
}
```

2. **发送文件元信息**
```c
// 发送文件名
train.length = strlen(filename) + 1;
memcpy(train.data, filename, train.length);
send(sockfd, &train, sizeof(train.length) + train.length, 0);

// 发送文件大小
train.length = sizeof(file_size);
memcpy(train.data, &file_size, train.length);
send(sockfd, &train, sizeof(train.length) + train.length, 0);

// 发送SHA-256值（用于秒传检测）
train.length=strlen(file_hash);
memcpy(train.data,file_hash,train.length);
send(sockfd,&train,sizeof(train.length)+train.length,0);
```

3. **服务端去重检查**
```c
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
        return;
    }
    send_msg(user->control_sockfd, "OK: File uploaded (instant transfer)\n");
    return; // 秒传完成
}
```

#### 1.2.4 SHA256 计算算法

```c
static int calculate_file_sha256_evp(char *filename, off_t offset, off_t length, char *sha256_str) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1) return -1;
    
    // 定位到指定偏移量
    if (lseek(fd, offset, SEEK_SET) < 0) {
        close(fd);
        return -1;
    }

    EVP_MD_CTX *sha256_ctx = EVP_MD_CTX_new();
    if (sha256_ctx == NULL) {
        close(fd);
        return -1;
    }
    
    // 初始化SHA256上下文
    if (EVP_DigestInit_ex(sha256_ctx, EVP_sha256(), NULL) != 1) {
        EVP_MD_CTX_free(sha256_ctx);
        close(fd);
        return -1;
    }

    char buf[BUF_SIZE] = {0};
    off_t total = 0;
    ssize_t n, to_read;
    
    // 循环读取文件并更新哈希
    while (total < length) {
        to_read = (length - total) > BUF_SIZE ? BUF_SIZE : (length - total);
        n = read(fd, buf, to_read);
        if (n == -1) {
            EVP_MD_CTX_free(sha256_ctx);
            close(fd);
            return -1;
        }
        
        if (EVP_DigestUpdate(sha256_ctx, buf, n) != 1) {
            EVP_MD_CTX_free(sha256_ctx);
            close(fd);
            return -1;
        }
        total += n;
    }

    // 完成哈希计算
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (EVP_DigestFinal_ex(sha256_ctx, digest, &digest_len) != 1) {
        EVP_MD_CTX_free(sha256_ctx);
        close(fd);
        return -1;
    }

    EVP_MD_CTX_free(sha256_ctx);
    close(fd);

    // 转换为十六进制字符串
    for (unsigned int i = 0; i < digest_len; ++i) {
        sprintf(&sha256_str[i * 2], "%02x", digest[i]);
    }
    sha256_str[digest_len * 2] = '\0';

    return 0;
}
```

### 1.3 秒传优势

1. **节省带宽**：避免重复文件的网络传输
2. **节省存储**：物理层面实现文件去重
3. **提升速度**：瞬间完成"上传"操作
4. **降低成本**：减少服务器存储和网络资源消耗

## 2. 断点续传原理

### 2.1 基本概念

断点续传（Resume Transfer）是指在文件传输过程中发生中断后，能够从上次传输的断点继续传输，而不需要重新开始传输整个文件。

### 2.2 下载断点续传实现

#### 2.2.1 客户端断点检测

```c
off_t resume_offset = 0; // 继续传输位置
// 检查是否已存在目标文件
int fd_local = open(filename, O_RDWR);
if (fd_local != -1) {
    // 文件存在，获取已下载大小
    struct stat st;
    if (fstat(fd_local, &st) == 0) {
        // 得到目前文件大小，也就是已下载的字节数，作为续传断点
        resume_offset = st.st_size;
    }
    close(fd_local);
}

// 将断点信息发送给服务端
train.length = sizeof(resume_offset);
memcpy(train.data, &resume_offset, train.length);
if (send(sockfd, &train, sizeof(train.length) + train.length, 0) == -1) {
    perror("send resume_offset");
    return -1;
}
```

#### 2.2.2 已下载部分完整性验证

```c
char sha256_local[SHA256_STR_LEN + 1] = {0};
if (resume_offset > 0) {
    // 计算已下载部分的SHA256值
    if (calculate_file_sha256_evp(filename, 0, resume_offset, sha256_local) != 0) {
        perror("calculate sha256");
        return -1;
    }
    
    // 发送SHA256给服务端验证
    train.length = strlen(sha256_local) + 1;
    memcpy(train.data, sha256_local, train.length);
    if (send(sockfd, &train, sizeof(train.length) + train.length, 0) == -1) {
        perror("send sha256_local");
        return -1;
    }
}
```

#### 2.2.3 服务端续传验证与处理

```c
// 如果resume_offset大于0，则进行续传校验
if (resume_offset > 0) {
    char client_sha256[SHA256_STR_LEN + 1] = {0};
    
    // 接收客户端发送来的已下载部分的SHA-256
    ret = recvn(user->data_sockfd, &train.length, sizeof(train.length));
    if (ret <= 0) {
        // 错误处理...
        return;
    }
    ret = recvn(user->data_sockfd, client_sha256, train.length);
    if (ret <= 0) {
        // 错误处理...
        return;
    }
    client_sha256[SHA256_STR_LEN] = '\0';
    
    char server_sha256[SHA256_STR_LEN + 1] = {0};
    // 计算服务端文件前resume_offset字节的SHA-256
    if (calculate_file_sha256_evp(fd, 0, resume_offset, server_sha256) != 0) {
        perror("calculate sha256");
        close(fd);
        free(transfer_arg);
        return;
    }
    
    // 比较客户端与服务端的SHA-256
    if (strcmp(client_sha256, server_sha256) != 0) {
        send_msg(user->control_sockfd,
                 "SHA-256 mismatch on resume portion, the entire file will be transferred");
        resume_offset = 0; // 不匹配则从头开始传输
    }
    
    // 将文件指针定位到续传位置
    if (lseek(fd, resume_offset, SEEK_SET) < 0) {
        perror("lseek");
        close(fd);
        free(transfer_arg);
        return;
    }
}
```

#### 2.2.4 文件传输优化策略

**大小文件分别处理**：

```c
#define LARGE_FILE_THRESHOLD 100 * 1024 * 1024 // 100MB阈值

// 根据文件大小选择传输策略
if (to_recv_size > LARGE_FILE_THRESHOLD) {
    // 大文件：使用mmap内存映射
    char *p = (char *)mmap(NULL, to_recv_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return -1;
    }
    
    // 直接映射到内存进行接收
    while (offset < to_recv_size) {
        ret = recvn(sockfd, p + offset, to_recv_size - offset);
        if (ret <= 0) break;
        offset += ret;
        print_progress(offset, to_recv_size); // 显示进度
    }
    munmap(p, to_recv_size);
    
} else {
    // 小文件：使用Train结构分块传输
    if (lseek(fd, resume_offset, SEEK_SET) == -1) {
        perror("lseek");
        close(fd);
        return -1;
    }
    
    off_t offset = resume_offset;
    while (1) {
        // 接收数据长度
        ret = recvn(sockfd, &train.length, sizeof(train.length));
        if (ret <= 0 || train.length == 0) break; // 传输结束
        
        // 接收数据内容
        ret = recvn(sockfd, train.data, train.length);
        if (ret <= 0) break;
        
        // 写入文件
        if (write(fd, train.data, train.length) == -1) {
            perror("write file");
            close(fd);
            return -1;
        }
        offset += train.length;
        print_progress(offset, to_recv_size);
    }
}
```

#### 2.2.5 传输完整性最终验证

```c
// 下载结束，对整个文件进行校验
char local_full_sha256[SHA256_STR_LEN + 1] = {0};
if (calculate_file_sha256_evp(filename, 0, to_recv_size, local_full_sha256) != 0) {
    perror("calculate full sha256");
    return -1;
}

// 接收服务端的完整文件SHA-256
char server_full_sha256[SHA256_STR_LEN + 1] = {0};
ret = recvn(sockfd, &train.length, sizeof(train.length));
if (ret > 0) {
    ret = recvn(sockfd, server_full_sha256, train.length);
    server_full_sha256[train.length] = '\0';
}

// 比较完整文件的SHA-256值
if (strcmp(local_full_sha256, server_full_sha256) == 0) {
    printf("File downloaded successfully and verified\n");
} else {
    printf("File verification failed\n");
    return -1;
}
```

### 2.3 Train 数据传输结构

```c
#define DATA_SIZE 4096

// 小火车结构，用于数据传输
typedef struct {
    int length;           // 数据长度（火车头）
    char data[DATA_SIZE]; // 数据内容（车厢）
} Train;
```

### 2.4 断点续传优势

1. **网络容错**：网络中断后可以继续传输，无需重新开始
2. **节省时间**：大文件传输中断后不需要从头开始
3. **节省流量**：避免重复传输已完成的部分
4. **用户体验**：提供传输进度显示和可恢复的传输过程

## 3. 技术特点总结

### 3.1 安全性保障

- **多重SHA256校验**：文件完整性、断点一致性、最终验证
- **分层验证机制**：上传前、传输中、传输后的多重校验
- **数据完整性**：确保传输过程中数据不被篡改

### 3.2 性能优化

- **零拷贝技术**：大文件使用 sendfile + mmap
- **自适应传输**：根据文件大小选择最优传输策略
- **内存映射**：减少用户态和内核态之间的数据拷贝

### 3.3 存储效率

- **物理去重**：相同文件只存储一份物理副本
- **虚拟文件系统**：用户级别的文件视图管理
- **空间节省**：显著减少存储空间占用

### 3.4 可靠性设计

- **错误恢复**：网络中断、系统崩溃后的自动恢复
- **数据一致性**：严格的校验机制确保数据一致性
- **异常处理**：完善的错误处理和资源清理机制

## 4. 应用场景

1. **云存储服务**：提供高效的文件上传下载体验
2. **大文件传输**：视频、镜像文件等大文件的可靠传输
3. **移动应用**：网络不稳定环境下的文件同步
4. **企业文档管理**：内部文件共享和版本管理

---

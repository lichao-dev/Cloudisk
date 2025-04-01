#include "ls_ll.h"

// qsort 比较函数，按字典序比较文件名
static int compare(const void *a, const void *b) {
    // 形参的a和b代表数组中的两个待比较元素的指针
    // 此处的两个待比较元素为字符串，即char *，因此
    // 这里的a和b是二级指针char **，即指向char*的指针
    // 强转后需要再解引用一次，才能得到指向字符串的指针
    const char *s1 = *(const char **)a;
    const char *s2 = *(const char **)b;

    // strcasecmp是忽略大小写的比较
    return strcasecmp(s1, s2);
}

// 处理ls命令
int handle_ls(UserInfo *user, char *dir) {
    // 构建目录路径
    char dir_path[PATH_MAX + 1] = {0};
    if (dir != NULL) {
        if (dir[0] == '~' && dir[1] == '/') {
            char *home_dir = get_home_directory();
            if (home_dir != NULL) {
                snprintf(dir_path, sizeof(dir_path), "%s/%s", home_dir, dir + 2); // 跳过~和/
            } else {
                send_msg(user->control_sockfd, "ERROR: Cannot determine home directory\n");
                CLOUDISK_LOG_INFO("%s: Cannot determine home directory",user->username);
                return -1;
            }
        } else if (dir[0] == '/') {
            // 绝对路径直接使用
            strncpy(dir_path, dir, sizeof(dir_path) - 1);
        } else {
            // 相对路径，拼接
            snprintf(dir_path, sizeof(dir_path), "%s/%s", user->current_dir, dir);
        }
    } else {
        // 如果ls后面没有跟参数，就ls用户当前目录
        strncpy(dir_path, user->current_dir, sizeof(dir_path) - 1);
    }
    // 打开目录
    DIR *dirp = opendir(dir_path);
    if (dirp == NULL) {
        // 如果打开失败，发送错误反馈给客户端
        send_err_msg(user->control_sockfd, "ls");
        CLOUDISK_LOG_ERROR_CHECK("ls");
        return -1;
    }

    // 用动态数组保存目录中各文件名字符串的指针
    // 该数组中每个元素是一个指针，指向了位于堆上的文件名字符串
    int capacity = 64; // 数组初始容量
    int count = 0;     // 数组当前元素个数
    char **file_names = malloc(capacity * sizeof(char *));
    if (file_names == NULL) {
        closedir(dirp);
        send_msg(user->control_sockfd, "ls: malloc failed");
        CLOUDISK_LOG_ERR("%s: malloc failed",user->username);
        return -1;
    }

    struct dirent *entry; // entry的意思是条目，此处这样命名表示一个目录条目
    while ((entry = readdir(dirp)) != NULL) {
        // 跳过"."和".."和隐藏文件，即以.开头的文件
        if (entry->d_name[0] == '.')
            continue;
        // 复制文件名到堆中，复制是为了防止每次调用readdir都会覆盖上一次结果
        char *name = strdup(entry->d_name);
        if (name == NULL) {
            continue; // 内存不足
        }

        // 如果目录条目超过了数组容量，就需要动态扩容
        if (count >= capacity) {
            capacity *= 2; // 默认扩容2倍空间
            char **temp = realloc(file_names, capacity * sizeof(char *));
            // 返回NULL表示内存不足，原始内存块保持不变可以继续访问
            if (temp == NULL) {
                free(name);
                continue;
            }
            file_names = temp;
        }
        file_names[count++] = name;
    }
    closedir(dirp);

    // 如果count==0，说明该目录下没有文件或子目录
    if (count == 0) {
        send_msg(user->control_sockfd, "NO files found\n");
        CLOUDISK_LOG_INFO("%s: NO files found",user->username);
        free(file_names);
        return -1;
    }

    // 对文件名进行排序
    qsort(file_names, count, sizeof(char *), compare);

    // 计算最长的文件名的长度
    int max_len = 0;
    for (int i = 0; i < count; ++i) {
        int name_len = strlen(file_names[i]);
        max_len = name_len > max_len ? name_len : max_len;
    }
    // 设置打印时的列宽,这是一个临时值，后面还要分别确定每列的列宽
    // 用col_widt是为了计算一行能放多少列
    int col_width = max_len;
    // 假设终端宽度为80个字符
    int term_width = 125;
    // cols表示每一行的列数
    int ncols = term_width / col_width;
    if (ncols < 1) { // 此时说明终端比较窄，一行放不下一列，那么就只在这一行放一列
        ncols = 1;
    }
    if (ncols > count) { // 此时说明，计算的列数大于实际的文件数，那么只需要列数和文件数相等
        ncols = count;
    }
    // 计算行数，向上取整
    int nrows = (count + ncols - 1) / ncols;

    // 计算每列最大的文件名长度
    int col_max_lens[ncols];
    for (int col = 0; col < ncols; col++) {
        col_max_lens[col] = 0;
        for (int row = 0; row < nrows; row++) {
            // 内层循环全部结束就遍历了一列，就确定了将来打印时位于同一列文件名的最大长度
            int idx = col * nrows + row; // col=0时，依次遍历file_names中0 1...nrows号元素
            if (idx < count) {
                int len = strlen(file_names[idx]);
                if (len > col_max_lens[col]) {
                    col_max_lens[col] = len;
                }
            }
        }
    }

    // 构造输出缓冲区
    char list_buf[LIST_BUF_SIZE] = {0}; // 定义一个足够大的数组用于存储最终的输出字符串
    int pos = 0;                        // 记录buf中当前写的位置
    // 实现ls命令的输出结果按照列排序
    for (int row = 0; row < nrows; row++) {
        for (int col = 0; col < ncols; col++) {
            // 先计算出当前要写入缓冲区的数组元素的下标
            // 文件名在file_names数组中是按照字典序连续存放的
            // 现在需要的ls输出结果是把数组中的文件名按照列打印出来
            // 即让数组中靠前的文件名在输出中靠前的列，模拟Linux系统的ls输出格式
            // 对于每一列，先填满整列，即nrows个元素，因此idx=col*nrows+row
            int idx = col * nrows + row;
            // 如果计算出的下标小于总文件数，说明数组中该位置有文件名，否则没有,下标越界
            if (idx < count) {
                // 使用snprintf将文件名写入list_buf中
                // 格式"%-*s"表示以固定宽度col_width左对齐输出字符串
                // * 是一个动态宽度占位符。它允许你在运行时通过参数指定字段的宽度
                // 而不是在格式字符串中硬编码一个固定值，在这里*对应的值是col_width
                // list_buf+pos表示从当前pos位置开始写入，剩余空间是sizeof(list_buf)-pos
                pos += snprintf(list_buf + pos, sizeof(list_buf) - pos, "%-*s",
                                col_max_lens[col] + 2, file_names[idx]);
            }
        }
        // 每处理完一行，添加换行符
        pos += snprintf(list_buf + pos, sizeof(list_buf) - pos, "\n");
    }
    // 将构造好的list_buf发送给客户端
    send_msg(user->control_sockfd, list_buf);

    // 释放申请的堆空间
    for (int i = 0; i < count; i++) {
        free(file_names[i]);
    }
    free(file_names);

    CLOUDISK_LOG_INFO("The ls command of %s was executed successfully",user->username);
    return 0;
}

// 处理ll命令
// int handle_ll(UserInfo *user){}

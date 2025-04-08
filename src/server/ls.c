#include "ls.h"

// qsort 比较函数，按字典序比较文件名
static int compare(const void *a, const void *b) {
    // 形参的a和b代表数组中的两个待比较元素的指针
    const FileRecord *r1 = (const FileRecord *)a;
    const FileRecord *r2 = (const FileRecord *)b;

    // strcasecmp是忽略大小写的比较
    return strcasecmp(r1->filename, r2->filename);
}

// 基于数据库的ls命令
int handle_ls_db(UserInfo *user, MYSQL *conn) {
    FileRecord *records = NULL;
    int record_count = 0;
    int user_id = get_user_id(conn, user->username);
    if (user_id < 0) {
        send_msg(user->control_sockfd, "ERROR: Cannot get user id\n");
        return -1;
    }

    // 获取当前虚拟目录的记录 ID
    int parent_id = get_dir_id(conn, user_id, user->current_dir);
    if (parent_id < 0) {
        send_msg(user->control_sockfd, "ERROR: Cannot get directory id\n");
        return -1;
    }

    // 查询当前目录下的文件记录
    if (file_db_list_records(conn, user_id, parent_id, &records, &record_count) != 0) {
        send_msg(user->control_sockfd, "ERROR: Query file records failed\n");
        return -1;
    }

    if (record_count == 0) {
        send_msg(user->control_sockfd, "No file found\n");
        return 0;
    }

    // 按文件名排序
    qsort(records, record_count, sizeof(FileRecord), compare);

    // 计算所有文件名的最大长度
    int max_len = 0;
    for (int i = 0; i < record_count; i++) {
        int len = strlen(records[i].filename);
        if (len > max_len) {
            max_len = len;
        }
    }
    // 每列列宽为最大长度加两个空格
    int col_width = max_len;
    // 假设终端宽度为125
    int term_width = 125;
    // cols表示每一行的列数
    int ncols = term_width / col_width;
    if (ncols < 1) {
        ncols = 1;
    }
    if (ncols > record_count) {
        ncols = record_count;
    }
    // 行数向上取整
    int nrows = (record_count + ncols - 1) / ncols;

    // 计算每列最大的文件名长度
    int cols_max_lens[ncols];
    for (int col = 0; col < ncols; col++) {
        cols_max_lens[col] = 0;
        for (int row = 0; row < nrows; row++) {
            int idx = col * nrows + row;
            if (idx < record_count) {
                int len = strlen(records[idx].filename);
                if (len > cols_max_lens[col])
                    cols_max_lens[col] = len;
            }
        }
    }

    // 构造输出缓冲区
    char list_buf[LIST_BUF_SIZE] = {0};
    int pos = 0;
    for (int row = 0; row < nrows; row++) {
        for (int col = 0; col < ncols; col++) {
            // 计算数组下标，采用列优先排序，第一列所有行，然后第二列。。。
            int idx = col * nrows + row;
            if (idx < record_count) {
                // 输出文件名，左对齐
                pos += snprintf(list_buf, LIST_BUF_SIZE - pos, "%-*s", cols_max_lens[col] + 2,
                                records[idx].filename);
            }
        }
        // 处理完一行，加换行符
        pos += snprintf(list_buf + pos, LIST_BUF_SIZE - pos, "\n");
    }

    // 将排序并排版后的ls输出发送给客户端
    send_msg(user->control_sockfd, list_buf);

    free(records);
    CLOUDISK_LOG_INFO("The ls command of %s was executed successfully", user->username);
    return 0;
}


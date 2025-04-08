#include "commands.h"
// 此模块是对简单命令的处理

// 构造两个辅助函数
// 给定完整路径，提取父目录和最后一级目录名
void extract_parent_and_dirname(const char *path, char *parent, size_t parent_size, char *dirname,
                                size_t dirname_size) {
    char *last_slash = strrchr(path, '/');
    if (last_slash != NULL) {
        if (last_slash == path) {
            // 如果路径仅为 "/"，父目录仍为 "/"
            strncpy(parent, "/", parent_size - 1);
        } else {
            size_t len = last_slash - path;
            strncpy(parent, path, len);
            parent[len] = '\0';
        }
        // 目录名为斜杠后面的部分
        strncpy(dirname, last_slash + 1, dirname_size - 1);
        dirname[dirname_size - 1] = '\0';
    } else {
        // 如果未找到斜杠，则父目录和目录名均设为 path
        strncpy(parent, path, parent_size - 1);
        strncpy(dirname, path, dirname_size - 1);
    }
}
// 根据当前目录、输入路径和用户名构造完整的虚拟路径
void normalize_path(const char *current, const char *input, const char *username, char *output,
                    size_t output_size) {
    char root[PATH_MAX] = {0};
    snprintf(root, sizeof(root), "/%s", username);

    // 处理空、"~" 或 "/"，直接返回虚拟根目录
    if (input == NULL || strlen(input) == 0 || strcmp(input, "~") == 0 || strcmp(input, "/") == 0) {
        strncpy(output, root, output_size - 1);
    }
    // 如果输入为 "."，则返回当前目录
    else if (strcmp(input, ".") == 0) {
        strncpy(output, current, output_size - 1);
    }
    // 如果输入为 ".."，返回当前目录的父目录（通过 extract_parent_and_dirname 得到）
    else if (strcmp(input, "..") == 0) {
        char parent[PATH_MAX] = {0};
        char dummy[FILENAME_SIZE] = {0};
        extract_parent_and_dirname(current, parent, sizeof(parent), dummy, sizeof(dummy));
        strncpy(output, parent, output_size - 1);
    }
    // 如果输入以 "~/" 开头，则转换为虚拟根目录加上后续路径
    else if (strncmp(input, "~/", 2) == 0) {
        snprintf(output, output_size, "%s/%s", root, input + 2);
    }
    // 如果是绝对路径（以 '/' 开头），直接使用
    else if (input[0] == '/') {
        strncpy(output, input, output_size - 1);
    }
    // 否则视为相对路径，与当前目录拼接
    else {
        snprintf(output, output_size, "%s/%s", current, input);
    }

    // 去除末尾多余的斜杠（如果 output 不是 "/"）
    size_t len = strlen(output);
    if (len > 1 && output[len - 1] == '/') {
        output[len - 1] = '\0';
    }
}

// 处理cd命令
int handle_cd_db(UserInfo *user, char *dir, MYSQL *conn) {

    char new_path[PATH_SIZE] = {0};
    normalize_path(user->current_dir, dir, user->username, new_path, sizeof(new_path));

    // 查询数据库，检查目标虚拟目录是否存在且类型为目录
    int user_id = get_user_id(conn, user->username);
    if (user_id < 0) {
        send_msg(user->control_sockfd, "User not found in database\n");
        return -1;
    }
    // 构造查询语句
    char query[FILE_DB_QUERY_SIZE];
    snprintf(query, sizeof(query),
             "SELECT id from file_info WHERE user_id = %d AND path = '%s' AND type = 'd'", user_id,
             new_path);
    if (mysql_query(conn, query) != 0) {
        send_msg(user->control_sockfd, "Database error\n");
        return -1;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    if (res == NULL) {
        send_msg(user->control_sockfd, "Database error\n");
        return -1;
    }
    int num_rows = mysql_num_rows(res);
    mysql_free_result(res);

    if (num_rows == 0) {
        send_msg(user->control_sockfd, "cd: No such directory\n");
        return -1;
    }

    // 更新当前用户的虚拟工作目录
    strncpy(user->current_dir, new_path, PATH_SIZE-1);
    user->current_dir[PATH_SIZE - 1] = '\0';
    send_msg(user->control_sockfd, "OK: Directory changed\n");
    CLOUDISK_LOG_INFO("%s: OK: Directory changed", user->username);

    return 0;
}

// 处理pwd命令的函数
int handle_pwd(UserInfo *user) {

    char cwd[PATH_MAX + 1]; // 存放当前目录的路径
    // 直接使用 user->current_dir 作为当前目录
    snprintf(cwd, sizeof(cwd), "%s\n", user->current_dir);
    // 发送pwd响应给客户端
    send_msg(user->control_sockfd, cwd);
    CLOUDISK_LOG_INFO("%s: Get the current directory successfully", user->username);

    return 0;
}

// mkdir
int handle_mkdir_db(UserInfo *user, char *dir, MYSQL *conn) {
    char new_path[PATH_SIZE] = {0};
    normalize_path(user->current_dir, dir, user->username, new_path, sizeof(new_path));

    // 提取父目录路径和新目录名称
    char parent_path[PATH_SIZE] = {0};
    char dirname[FILENAME_SIZE] = {0};
    extract_parent_and_dirname(new_path, parent_path, sizeof(parent_path), dirname,
                               sizeof(dirname));

    // 获取当前用户的 id
    int user_id = get_user_id(conn, user->username);
    if (user_id < 0) {
        send_msg(user->control_sockfd, "User not found in database\n");
        return -1;
    }

    // 查询数据库，利用 parent_path 查找父目录记录（类型必须为 'd'）
    char query[FILE_DB_QUERY_SIZE] = {0};
    snprintf(query, sizeof(query),
             "SELECT id FROM file_info WHERE user_id = %d AND path = '%s' AND type = 'd'", user_id,
             parent_path);
    if (mysql_query(conn, query) != 0) {
        send_msg(user->control_sockfd, "Database error\n");
        return -1;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    if (res == NULL) {
        send_msg(user->control_sockfd, "Database error\n");
        return -1;
    }
    int num_rows = mysql_num_rows(res);
    if (num_rows == 0) {
        mysql_free_result(res);
        send_msg(user->control_sockfd, "mkdir: parent directory not found\n");
        return -1;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    int parent_id = atoi(row[0]);
    mysql_free_result(res);

    // 构造新的目录记录
    FileRecord record;
    memset(&record, 0, sizeof(record));
    record.user_id = user_id;
    record.parent_id = parent_id;
    strncpy(record.filename, dirname, FILENAME_SIZE - 1);
    strncpy(record.path, new_path, PATH_SIZE - 1);
    record.type = 'd';
    record.filesize = 0;     // 目录无文件大小
    record.sha256[0] = '\0'; // 目录无哈希值

    // 插入记录到数据库
    if (file_db_create_record(conn, &record) != 0) {
        send_msg(user->control_sockfd, "mkdir: create directory failed\n");
        return -1;
    }

    send_msg(user->control_sockfd, "OK: Directory created\n");
    CLOUDISK_LOG_INFO("%s: Directory %s created", user->username, new_path);

    return 0;
}

// rmdir
int handle_rmdir_db(UserInfo *user, char *dir, MYSQL *conn) {
    char new_path[PATH_SIZE] = {0};
    // 构造完整虚拟路径
    normalize_path(user->current_dir, dir, user->username, new_path, sizeof(new_path));

    int user_id = get_user_id(conn, user->username);
    if (user_id < 0) {
        send_msg(user->control_sockfd, "User not found in database\n");
        return -1;
    }

    // 查询数据库获取待删除目录的记录（要求 type = 'd'）
    char query[FILE_DB_QUERY_SIZE] = {0};
    snprintf(
        query, sizeof(query),
        "SELECT id, parent_id FROM file_info WHERE user_id = %d AND path = '%s' AND type = 'd'",
        user_id, new_path);
    if (mysql_query(conn, query) != 0) {
        send_msg(user->control_sockfd, "Database error\n");
        return -1;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    if (res == NULL) {
        send_msg(user->control_sockfd, "Database error\n");
        return -1;
    }
    int num_rows = mysql_num_rows(res);
    if (num_rows == 0) {
        mysql_free_result(res);
        send_msg(user->control_sockfd, "rmdir: directory not found\n");
        return -1;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    int dir_id = atoi(row[0]);
    mysql_free_result(res);

    // 检查目录是否为空（不存在子记录）
    snprintf(query, sizeof(query), "SELECT id FROM file_info WHERE user_id = %d AND parent_id = %d",
             user_id, dir_id);
    if (mysql_query(conn, query) != 0) {
        send_msg(user->control_sockfd, "Database error\n");
        return -1;
    }
    res = mysql_store_result(conn);
    if (res == NULL) {
        send_msg(user->control_sockfd, "Database error\n");
        return -1;
    }
    num_rows = mysql_num_rows(res);
    mysql_free_result(res);

    if (num_rows > 0) {
        send_msg(user->control_sockfd, "rmdir: directory not empty\n");
        return -1;
    }

    // 删除目录记录
    if (file_db_delete_record(conn, user_id, dir_id) != 0) {
        send_msg(user->control_sockfd, "rmdir: delete failed\n");
        return -1;
    }

    // 如果当前工作目录正好为删除目录，则更新为父目录
    if (strcmp(user->current_dir, new_path) == 0) {
        char parent_path[PATH_MAX] = {0};
        char dummy[FILENAME_SIZE] = {0};
        extract_parent_and_dirname(new_path, parent_path, sizeof(parent_path), dummy,
                                   sizeof(dummy));
        strncpy(user->current_dir, parent_path, PATH_MAX - 1);
        user->current_dir[PATH_MAX - 1] = '\0';
    }

    send_msg(user->control_sockfd, "OK: Directory removed\n");
    CLOUDISK_LOG_INFO("%s: OK: Directory %s removed", user->username, new_path);

    return 0;
}

// rm
int handle_rm_db(UserInfo *user, char *file, MYSQL *conn) {

    char new_path[PATH_SIZE] = {0};

    // 构造完整虚拟路径
    normalize_path(user->current_dir, file, user->username, new_path, sizeof(new_path));

    int user_id = get_user_id(conn, user->username);
    if (user_id < 0) {
        send_msg(user->control_sockfd, "Internal error: user not found in database\n");
        return -1;
    }

    // 查询数据库，查找该文件记录，要求 type = 'f'
    char query[FILE_DB_QUERY_SIZE] = {0};
    snprintf(query, sizeof(query),
             "SELECT id FROM file_info WHERE user_id = %d AND path = '%s' AND type = 'f'", user_id,
             new_path);
    if (mysql_query(conn, query) != 0) {
        send_msg(user->control_sockfd, "Database error\n");
        return -1;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    if (res == NULL) {
        send_msg(user->control_sockfd, "Database error\n");
        return -1;
    }
    int num_rows = mysql_num_rows(res);
    if (num_rows == 0) {
        mysql_free_result(res);
        send_msg(user->control_sockfd, "rm: file not found\n");
        return -1;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    int record_id = atoi(row[0]);
    mysql_free_result(res);

    // 删除文件记录
    if (file_db_delete_record(conn, user_id, record_id) != 0) {
        send_msg(user->control_sockfd, "rm: delete failed\n");
        return -1;
    }

    send_msg(user->control_sockfd, "OK: File removed\n");
    CLOUDISK_LOG_INFO("%s: OK: File %s removed", user->username, new_path);

    return 0;
}

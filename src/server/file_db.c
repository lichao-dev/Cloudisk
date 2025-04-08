#include "file_db.h"

// 创建新记录（文件或目录）
int file_db_create_record(MYSQL *conn, const FileRecord *record) {
    char esc_filename[FILENAME_SIZE] = {0};
    char esc_path[PATH_SIZE] = {0};
    char esc_sha256[SHA256_SIZE] = {0};
    // 转义，防止注入攻击
    mysql_real_escape_string(conn, esc_filename, record->filename, strlen(record->filename));
    mysql_real_escape_string(conn, esc_path, record->path, strlen(record->path));
    if (record->sha256[0] != '\0') {
        mysql_real_escape_string(conn, esc_sha256, record->sha256, strlen(record->sha256));
    }

    // 拼接SQL插入语句
    char query[FILE_DB_QUERY_SIZE];
    snprintf(query, sizeof(query),
             "INSERT INTO file_info (filename, user_id, parent_id, path, type, sha256, filesize) "
             "VALUES ('%s', %d, %d, '%s', '%c', '%s', %ld)",
             esc_filename, record->user_id, record->parent_id, esc_path, record->type, esc_sha256,
             record->filesize);
    // 执行语句
    if (mysql_query(conn, query) != 0) {
        MYSQL_ERROR_PRINT(conn, "file_db_create_record: insert failed");
        return -1;
    }
    return 0;
}

// 查询指定目录下的所有文件记录
int file_db_list_records(MYSQL *conn, int user_id, int parent_id, FileRecord **records,
                          int *record_count) {
    // 构造 SQL 查询语句，查询给定用户和父目录下的记录
    char query[FILE_DB_QUERY_SIZE];
    snprintf(query, sizeof(query),
             "SELECT id, filename, user_id, parent_id, path, type, sha256, filesize, upload_time "
             "FROM file_info WHERE user_id = %d AND parent_id = %d",
             user_id, parent_id);

    // 执行查询
    if (mysql_query(conn, query) != 0) {
        MYSQL_ERROR_PRINT(conn, "file_db_list_records: query failed");
        return -1;
    }

    // 获取查询结果集
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res) {
        MYSQL_ERROR_PRINT(conn, "file_db_list_records: store result failed");
        return -1;
    }

    // 获取查询到的记录数
    int num_rows = mysql_num_rows(res);
    *record_count = num_rows;
    if (num_rows == 0) {
        // 若没有查询到记录，释放结果集，设置输出指针为 NULL，并返回成功
        mysql_free_result(res);
        *records = NULL;
        return 0;
    }

    // 动态分配足够的内存，用于存储所有查询到的 FileRecord 记录
    *records = (FileRecord *)malloc(num_rows * sizeof(FileRecord));
    if (*records == NULL) {
        mysql_free_result(res);
        return -1;
    }

    // 遍历查询结果，每一行对应一条记录
    MYSQL_ROW row;
    int idx = 0;
    while ((row = mysql_fetch_row(res))) {
        // 将数据库中查询到的各字段转换并赋值到 FileRecord 结构体中
        (*records)[idx].id = atoi(row[0]); // id字段转换为整数
        // 复制文件名，确保不超过预定义缓冲区长度
        strncpy((*records)[idx].filename, row[1], FILENAME_SIZE - 1);
        (*records)[idx].user_id = atoi(row[2]);
        (*records)[idx].parent_id = atoi(row[3]);
        // 复制完整路径
        strncpy((*records)[idx].path, row[4], PATH_SIZE - 1);
        // 类型字段，第一个字符代表类型
        (*records)[idx].type = row[5][0];
        // 如果SHA-256字段不为空，则复制，否则设置为空字符串(目录的该字段为空)
        if (row[6]) {
            strncpy((*records)[idx].sha256, row[6], SHA256_SIZE - 1);
        } else {
            (*records)[idx].sha256[0] = '\0';
        }
        // 文件大小转换为 long 类型
        (*records)[idx].filesize = atol(row[7]);
        idx++;
    }

    // 释放查询结果集资源
    mysql_free_result(res);
    return 0;
}

// 删除指定用户的文件记录
int file_db_delete_record(MYSQL *conn, int user_id, int record_id) {
    // 构造 SQL 删除语句，只删除符合 user_id 和 record_id 的记录
    char query[FILE_DB_QUERY_SIZE];
    snprintf(query, sizeof(query),
             "DELETE FROM file_info WHERE id = %d AND user_id = %d",
             record_id, user_id);
    // 执行删除操作
    if (mysql_query(conn, query) != 0) {
        MYSQL_ERROR_PRINT(conn, "file_db_delete_record: delete failed");
        return -1;
    }
    return 0;
}

// 更新指定用户某记录的文件名和完整路径,可用于重命名或移动文件
int file_db_update_record(MYSQL *conn, int user_id, int record_id, const char *new_filename, const char *new_path) {
    // 定义缓冲区用于保存转义后的新文件名和路径
    char esc_filename[FILENAME_SIZE] = {0};
    char esc_path[PATH_SIZE] = {0};

    // 对 new_filename 和 new_path 进行 SQL 转义
    mysql_real_escape_string(conn, esc_filename, new_filename, strlen(new_filename));
    mysql_real_escape_string(conn, esc_path, new_path, strlen(new_path));

    // 构造 SQL 更新语句
    char query[FILE_DB_QUERY_SIZE];
    snprintf(query, sizeof(query),
             "UPDATE file_info SET filename = '%s', path = '%s' WHERE id = %d AND user_id = %d",
             esc_filename, esc_path, record_id, user_id);
    // 执行更新操作，如果失败则打印错误信息
    if (mysql_query(conn, query) != 0) {
        MYSQL_ERROR_PRINT(conn, "file_db_update_record: update failed");
        return -1;
    }
    return 0;
}

// 根据文件 SHA256 值在数据库中查找是否已存在相同文件（物理存储唯一）。
// 如果找到则将对应记录存入 record，返回 0；否则返回 -1。
int file_db_find_by_hash(MYSQL *conn, const char *sha256, FileRecord *record){
    char query[FILE_DB_QUERY_SIZE];
    snprintf(query,sizeof(query),
             "SELECT id, filename, user_id, parent_id, path, type, sha256, filesize "
             "FROM file_info WHERE sha256 = '%s' LIMIT 1",sha256);
    if(mysql_query(conn,query)!=0){
        MYSQL_ERROR_PRINT(conn,"mysql_query by sha256 failed");
        return -1;
    }
    MYSQL_RES *res=mysql_store_result(conn);
    if(res==NULL){
        MYSQL_ERROR_PRINT(conn, "store result failed");
        return -1;
    }
    int num=mysql_num_rows(res);
    if(num==0){
        mysql_free_result(res);
        return -1;
    }
    MYSQL_ROW row=mysql_fetch_row(res);
    record->id=atoi(row[0]);
    strncpy(record->filename,row[1],FILENAME_SIZE-1);
    record->user_id=atoi(row[2]);
    record->parent_id=atoi(row[3]);
    strncpy(record->path,row[4],PATH_SIZE-1);
    record->type=row[5][0];
    strncpy(record->sha256,row[6],SHA256_STR_LEN);
    record->filesize=atoi(row[7]);
    mysql_free_result(res);
    return 0;
}

// 根据用户ID和虚拟路径查询数据库中的文件记录，
// 如果存在，则返回记录内容（记录中的 sha256 字段即为物理文件名）
int file_db_get_record_by_path(MYSQL *conn, int user_id, const char *virtual_path, FileRecord *record){
    char query[FILE_DB_QUERY_SIZE];
    snprintf(query,sizeof(query),
             "SELECT id, filename, user_id, parent_id, path, type, sha256, filesize "
             "FROM file_info WHERE user_id = %d AND path = '%s' AND type = 'f' LIMIT 1",
             user_id,virtual_path);
    if(mysql_query(conn,query)!=0){
        MYSQL_ERROR_PRINT(conn,"mysql_query record by path");
        return -1;
    }
    MYSQL_RES *res=mysql_store_result(conn);
    if(res==NULL){
        MYSQL_ERROR_PRINT(conn,"mysql_store_result failed");
        return -1;
    }
    int num = mysql_num_rows(res);
    if(num==0){
        mysql_free_result(res);
        return -1;
    }
    MYSQL_ROW row=mysql_fetch_row(res);
    
    record->id=atoi(row[0]);
    strncpy(record->filename, row[1], FILENAME_SIZE - 1);
    record->user_id = atoi(row[2]);
    record->parent_id = atoi(row[3]);
    strncpy(record->path, row[4], PATH_SIZE - 1);
    record->type = row[5][0];
    strncpy(record->sha256, row[6], SHA256_STR_LEN);
    record->filesize = atol(row[7]);
    mysql_free_result(res);

    return 0;
 }

// 获取目录id
int get_dir_id(MYSQL *conn, int user_id, const char *current_dir){
    char query[FILE_DB_QUERY_SIZE];
    snprintf(query, sizeof(query),
             "SELECT id FROM file_info WHERE user_id = %d AND path = '%s' AND type = 'd' LIMIT 1",
             user_id, current_dir);
    if (mysql_query(conn, query) != 0) {
        return -1;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    if (!res || mysql_num_rows(res) == 0) {
        if (res) mysql_free_result(res);
        return -1;
    }
    MYSQL_ROW row = mysql_fetch_row(res);
    int id = atoi(row[0]);
    mysql_free_result(res);
    return id;
}

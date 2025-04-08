#ifndef FILE_DB_H
#define FILE_DB_H

#include <cloudisk.h>

#define FILENAME_SIZE 256
#define PATH_SIZE 512
#define SHA256_SIZE 65
#define FILE_DB_QUERY_SIZE 1024  // 所有查询语句使用的统一缓冲区大小
#define SHA256_STR_LEN 64   // SHA-256 十六进制字符串长度

// 插入新文件
typedef struct {
    int id;                       // 文件记录ID
    char filename[FILENAME_SIZE]; // 用户上传的原始文件名
    int user_id;                  // 所属用户ID
    int parent_id;                // 父目录ID(0表示根目录)
    char path[PATH_MAX];         // 完整虚拟路径
    char type;                    // 'f'表示文件，'d'表示目录
    char sha256[SHA256_SIZE];     // 文件内容的SHA-256值（用于秒传或校验）
    long filesize;                // 文件大小
} FileRecord;

// 创建一个新的文件或目录记录
int file_db_create_record(MYSQL *conn, const FileRecord *record);
// 查询指定目录下的所有文件记录
int file_db_list_records(MYSQL *conn, int user_id, int parent_id, FileRecord **records,
                          int *record_count);
// 删除指定文件记录
int file_db_delete_record(MYSQL *conn, int user_id, int record_id);
// 更新指定文件记录的文件名和路径
int file_db_update_record(MYSQL *conn, int user_id, int record_id, const char *new_filename,
                          const char *new_path);
// 根据文件 SHA256 值在数据库中查找是否已存在相同文件（物理存储唯一）。
// 如果找到则将对应记录存入 record，返回 0；否则返回 -1。
int file_db_find_by_hash(MYSQL *conn, const char *sha256, FileRecord *record);
// 根据用户ID和虚拟路径查询数据库中的文件记录，
// 如果存在，则返回记录内容（记录中的 sha256 字段即为物理文件名）
int file_db_get_record_by_path(MYSQL *conn, int user_id, const char *virtual_path, FileRecord *record);
// 获取目录id
int get_dir_id(MYSQL *conn, int user_id, const char *current_dir);

#endif

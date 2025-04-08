#ifndef DB_H
#define DB_H

#include <cloudisk.h>

// SQL 语句缓冲区的最大长度（用于拼接 CREATE/INSERT 等语句）
#define SQL_BUF_LEN 256
// 数据库名称的最大长度
#define DB_NAME_MAX 64

// 数据库初始化函数(每个线程调用一次)
MYSQL *db_init(const char *host,const char *user,const char *password,const char *dbname);
// 创建数据库中必要的表结构，只需主线程调用一次
int db_create_tables(MYSQL *conn);
// 关闭数据库连接
void db_close(MYSQL *conn);

#endif

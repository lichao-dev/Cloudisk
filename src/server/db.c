#include "db.h"

// 初始化数据库连接
// host-主机名，user-用户名，dbname-数据库名
MYSQL *db_init(const char *host, const char *user, const char *password, const char *dbname) {
    MYSQL *conn; // 表示一个数据库连接

    // MySQL初始化和连接线程不安全，需要加锁
    // 此处静态局部变量，存放在数据段，只会初始化一次，后续线程直接使用
    static pthread_mutex_t init_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&init_lock);

    conn = mysql_init(NULL);
    if (conn == NULL) {
        MYSQL_ERROR_PRINT(conn, "mysql_init failed");
        pthread_mutex_unlock(&init_lock);
        return NULL;
    }
    // 连接数据库服务器，这里不指定数据库，因为要动态创建数据库，后面创建后再指定
    if (mysql_real_connect(conn, host, user, password, NULL, 0, NULL, 0) == NULL) {
        MYSQL_ERROR_PRINT(conn, "mysql_real_connect failed");
        mysql_close(conn);
        pthread_mutex_unlock(&init_lock);
        return NULL;
    }
    pthread_mutex_unlock(&init_lock);

    // 创建数据库（如果不存在）
    char create_db_sql[SQL_BUF_LEN];
    snprintf(create_db_sql, SQL_BUF_LEN, "CREATE DATABASE IF NOT EXISTS %s CHARACTER SET utf8mb4;",
             dbname);
    // 执行,成功0，失败非0
    if (mysql_query(conn, create_db_sql) != 0) {
        MYSQL_ERROR_PRINT(conn, "Create database failed");
        mysql_close(conn);
        return NULL;
    }

    // 切换到指定数据库
    if (mysql_select_db(conn, dbname) != 0) {
        MYSQL_ERROR_PRINT(conn, "Select database failed");
        mysql_close(conn);
        return NULL;
    }

    return conn;
}

// 创建数据库中所需的表结构（用户表和文件表）
int db_create_tables(MYSQL *conn) {
    // 用户表
    // 构造多行字符串常量
    // id为自增主键，用户名不能重复，
    const char *user_table = "CREATE TABLE IF NOT EXISTS user ("
                             "id INT PRIMARY KEY AUTO_INCREMENT,"
                             "username VARCHAR(64) NOT NULL,"
                             "salt VARCHAR(64) NOT NULL,"
                             "encrypted_password VARCHAR(256) NOT NULL,"
                             "is_deleted BOOLEAN DEFAULT 0,"
                             "create_time DATETIME DEFAULT CURRENT_TIMESTAMP,"
                             "UNIQUE KEY idx_user_valid (username, is_deleted));";
    // 文件表
    const char *file_table = "CREATE TABLE IF NOT EXISTS file_info ("
                             "id INT PRIMARY KEY AUTO_INCREMENT,"
                             "filename VARCHAR(255) NOT NULL,"
                             "user_id INT NOT NULL,"
                             "parent_id INT DEFAULT 0,"
                             "path VARCHAR(512) NOT NULL,"
                             "type CHAR(1) NOT NULL COMMENT 'f = file, d = directory',"
                             "sha256 CHAR(64),"
                             "filesize BIGINT DEFAULT 0,"
                             "upload_time DATETIME DEFAULT CURRENT_TIMESTAMP,"
                             "FOREIGN KEY (user_id) REFERENCES user(id));";

    // 执行建表语句
    if (mysql_query(conn, user_table) != 0) {
        MYSQL_ERROR_PRINT(conn, "Create user table failed");
        return -1;
    }

    if (mysql_query(conn, file_table)) {
        MYSQL_ERROR_PRINT(conn, "Create file_info table failed");
        return -1;
    }
    return 0;
}

// 关闭数据库连接
void db_close(MYSQL *conn) {
    if (conn != NULL) {
        mysql_close(conn); // 释放连接资源
    }
}

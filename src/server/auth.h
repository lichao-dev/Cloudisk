#ifndef AUTH_H
#define AUTH_H

#include <cloudisk.h>
#include "msg.h"
#include "session.h"

#define SALT_LEN 16           // 盐长度（不包含前缀）
#define FULL_SALT_LEN 32      // 完整盐格式长度，包含 $6$ 等前缀
#define HASH_LEN 256          // 密码加密后最长输出长度
#define USERNAME_MAX_LEN 128  // 用户名最大长度（转义后）
#define SQL_QUERY_MAX_LEN 256 // SQL 查询语句最大长度

// 注册用户
int register_user(MYSQL *conn, int sockfd, const char *username, const char *password);
// 用户登录
int login_user(MYSQL *conn, int sockfd, const char *username, const char *encrypted_password);
// 获取用户id
int get_user_id(MYSQL *conn,const char *username);
// 注销用户(逻辑删除)
int delete_user(MYSQL *conn,const char *username);
// 获取指定用户的salt
int get_user_salt(MYSQL *conn, const char *username, char *salt_buf, size_t buf_size);

#endif

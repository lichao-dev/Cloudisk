#include "auth.h"

// 生成随机salt，并格式化为"$6$xxxx$"格式
void generate_salt(char *salt) {
    const char *salt_prefix = "$6$"; // 使用SHA-512
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";
    char raw_salt[SALT_LEN + 1] = {0}; // 初始的salt

    // 使用时间戳初始化随机种子，防止重复
    srand((unsigned int)time(NULL));

    for (int i = 0; i < SALT_LEN; ++i) {
        raw_salt[i] = charset[rand() % (sizeof(charset) - 1)];
    }

    snprintf(salt, FULL_SALT_LEN, "%s%s$", salt_prefix, raw_salt);
}

// 检查用户是否存在
int user_exists(MYSQL *conn, const char *username) {
    char escaped_username[USERNAME_MAX_LEN]; // 存储转义后的用户名，避免SQL注入风险
    mysql_real_escape_string(conn, escaped_username, username, strlen(username));

    char query[SQL_QUERY_MAX_LEN]; // 构造查询语句
    snprintf(query, sizeof(query), "SELECT id FROM user WHERE username = '%s' AND is_deleted = 0",
             escaped_username);
    // 执行查询
    if (mysql_query(conn, query) != 0) {
        MYSQL_ERROR_PRINT(conn, "user_exists: query failed");
        return -1;
    }

    // 获取查询结果集
    MYSQL_RES *res = mysql_store_result(conn);
    if (res == NULL) {
        MYSQL_ERROR_PRINT(conn, "user_exists: store result failed");
        return -1;
    }

    // 判断是否有返回记录（存在此用户）
    int exists = (mysql_num_rows(res) > 0);
    mysql_free_result(res);
    return exists;
}

// 用户注册
int register_user(MYSQL *conn, int sockfd, const char *username, const char *password) {
    // 检查用户是否已经存在
    if (user_exists(conn, username)) {
        fprintf(stderr, "[Register] Username already exists.\n");
        send_msg(sockfd,"[Register] Username already exists\n");  
        return 1;
    }

    char salt[FULL_SALT_LEN];
    generate_salt(salt); // 生成随机盐值

    // 使用crypt加密密码
    char *encrypted = crypt(password, salt);
    if (encrypted == NULL) {
        fprintf(stderr, "[Register] crypt failed.\n");
        send_msg(sockfd,"[Register] crypt failed\n");
        return -1;
    }

    // 对用户名、密码、盐值进行转义，防止注入攻击
    char esc_user[USERNAME_MAX_LEN];
    char esc_pass[HASH_LEN];
    char esc_salt[FULL_SALT_LEN];
    mysql_real_escape_string(conn, esc_user, username, strlen(username));
    mysql_real_escape_string(conn, esc_pass, encrypted, strlen(encrypted));
    mysql_real_escape_string(conn, esc_salt, salt, strlen(salt));

    // 拼接 SQL 插入语句，插入用户名、加密密码和盐
    char insert_sql[SQL_QUERY_MAX_LEN * 2];
    snprintf(insert_sql, sizeof(insert_sql),
             "INSERT INTO user (username, encrypted_password, salt) VALUES ('%s', '%s', '%s')", esc_user,
             esc_pass, esc_salt);
    // 执行插入语句
    if (mysql_query(conn, insert_sql) != 0) {
        MYSQL_ERROR_PRINT(conn, "register_user: insert failed");
        send_msg(sockfd,"[Register] Register failed\n");
        return -1;
    }

    // 注册成功
    printf("[Register] User '%s' registered successfully.\n", username);
    return 0;
}

// 用户登录
// 返回值：0成功，-1失败
int login_user(MYSQL *conn, int sockfd, const char *username, const char *encrypted_password) {
    char esc_user[USERNAME_MAX_LEN];
    mysql_real_escape_string(conn, esc_user, username, strlen(username));

    // 构造SQL查询语句，获取该用户的加密密码
    char query[SQL_QUERY_MAX_LEN];
    snprintf(query, sizeof(query),
             "SELECT encrypted_password FROM user WHERE username = '%s' AND is_deleted = 0",
             esc_user);
    // 执行查询
    if (mysql_query(conn, query) != 0) {
        MYSQL_ERROR_PRINT(conn, "login_user: query failed");
        return -1;
    }

    // 获取查询结果集
    MYSQL_RES *res = mysql_store_result(conn);
    if (res == NULL) {
        MYSQL_ERROR_PRINT(conn, "login_user: store result failed");
        return -1;
    }

    // 如果找不到该用户，登录失败
    if (mysql_num_rows(res) == 0) {
        mysql_free_result(res);
        fprintf(stderr, "[Login] User not found.\n");
        send_msg(sockfd,"[Login] User not exists\n");
        return -1;
    }

    // 提取数据库中的密码盒盐值
    MYSQL_ROW row = mysql_fetch_row(res);
    const char *stored_hash = row[0]; // 加密后的密码
    
    printf("login_user: \nstored_hash:%s\n",stored_hash);
    printf("login_user: \nencrypted_password:%s\n",encrypted_password);

    // 对比用户输入的加密后的密码和数据库的加密密码
    int result = strcmp(encrypted_password, stored_hash) == 0 ? 0 : -1;

    if (result == 0) {
        printf("[Login] User '%s' login successful.\n", username);
    } else {
        fprintf(stderr, "[Login] Invalid password for user '%s'.\n", username);
        send_msg(sockfd,"[Login] Incorrect password\n");
    }

    mysql_free_result(res);

    return result;
}

// 注销用户(逻辑删除)
// 成功返回0，失败返回-1
int delete_user(MYSQL *conn, const char *username) {
    char esc_user[USERNAME_MAX_LEN];
    // 对用户名进行转义，防止 SQL 注入
    mysql_real_escape_string(conn, esc_user, username, strlen(username));

    // 构造更新语句，将is_deleted设为1
    char sql[SQL_QUERY_MAX_LEN];
    snprintf(sql, sizeof(sql), "UPDATE user SET is_deleted = 1 WHERE username = '%s'", esc_user);

    // 执行更新操作
    if (mysql_query(conn, sql) != 0) {
        MYSQL_ERROR_PRINT(conn, "delete_user: update failed");
        return -1;
    }

    // 打印成功提示
    printf("[Delete] User '%s' has been deleted.\n", username);
    return 0;
}

// 获取用户id
int get_user_id(MYSQL *conn, const char *username) {
    char esc_user[USERNAME_MAX_LEN];
    // 对用户名进行转义，防止 SQL 注入
    mysql_real_escape_string(conn, esc_user, username, strlen(username));

    // 构造查询语句，查找用户id
    char query[SQL_QUERY_MAX_LEN];
    snprintf(query, sizeof(query), "SELECT id FROM user WHERE username = '%s' AND is_deleted = 0",
             esc_user);

    // 执行SQL查询
    if (mysql_query(conn, query) != 0) {
        MYSQL_ERROR_PRINT(conn, "get_user_id: query failed");
        return -1;
    }

    // 获取查询结果
    MYSQL_RES *res = mysql_store_result(conn);
    if (res == NULL) {
        MYSQL_ERROR_PRINT(conn, "get_user_id: store result failed");
        return -1;
    }
    // 如果没找到
    if (mysql_num_rows(res) == 0) {
        mysql_free_result(res);
        fprintf(stderr, "get_user_id: no result\n");
        return -1;
    }

    // 提取用户id并转换为整数
    MYSQL_ROW row = mysql_fetch_row(res);
    int id = atoi(row[0]);
    mysql_free_result(res);
    return id;
}

// 获取指定用户的salt
int get_user_salt(MYSQL *conn, const char *username, char *salt_buf, size_t buf_size) {
    char esc_user[USERNAME_MAX_LEN];
    mysql_real_escape_string(conn, esc_user, username, strlen(username));
    
    // 构造查询语句，查找salt
    char query[SQL_QUERY_MAX_LEN];
    snprintf(query, sizeof(query), "SELECT salt FROM user WHERE username = '%s' AND is_deleted = 0",
             esc_user);

    if (mysql_query(conn, query) != 0) {
        MYSQL_ERROR_PRINT(conn, "get_user_salt: query failed");
        return -1;
    }

    MYSQL_RES *res = mysql_store_result(conn);
    if (res == NULL) {
        MYSQL_ERROR_PRINT(conn, "get_user_id: store result failed");
        return -1;
    }
    // 如果没找到
    if (mysql_num_rows(res) == 0) {
        mysql_free_result(res);
        fprintf(stderr, "get_user_id: no result");
        return -1;
    }
    
    // 提取salt
    MYSQL_ROW row = mysql_fetch_row(res);
    strncpy(salt_buf, row[0], buf_size - 1);
    salt_buf[buf_size - 1] = '\0'; // 确保结尾安全
    mysql_free_result(res);
    return 0;
}

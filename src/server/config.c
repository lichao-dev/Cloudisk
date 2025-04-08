#include "config.h"

ServerConfig config;

static void trim_whitespace(char *str) {

    // 去除头部空白字符
    char *start = str;
    // *start 检查是否到达字符串末尾（\0），若为 0，则停止
    // isspace((unsigned char)*start) 检查当前字符是否是空白字符（如 ' ', \n, \t 等）
    // 如果是空白字符，start++ 移动到下一个字符
    // 循环结束，start指向第一个非空白字符
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != str) { // 说明start移动了
        // memmove函数实现了将start开始的字符挪到str字符串开头
        memmove(str, start, strlen(start) + 1);
    }

    // 去除尾部空白字符
    size_t len = strlen(str); // len的大小包括了换行符
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        // 从尾部向前 重复直到遇到非空白字符或字符串变空
        str[len - 1] = '\0';
        len--;
    }
}

int load_config(const char *conf_name) {
    // 打开配置文件
    FILE *fp = fopen(conf_name, "r");
    if (!fp) {
        perror("fopen config file");
        CLOUDISK_LOG_ERROR_CHECK("fopen config file");
        return -1;
    }
    // line数组中存放的是从配置文件读取的一行文本
    char line[MAX_CONFIG_LINE] = {0};
    // 初始化配置结构体为零
    memset(&config, 0, sizeof(ServerConfig));

    // 读取配置文件的一行
    while (fgets(line, sizeof(line), fp)) {
        // 去除前后空白(包括换行)
        trim_whitespace(line);

        // 跳过空行或以#开头的注释
        if (line[0] == '\0' || line[0] == '#')
            continue;

        // 解析key=value格式
        char key[KEY_LEN] = {0};     // 键数组
        char value[VALUE_LEN] = {0}; // 值数组
        // %63[^=]:读取最多63个字符(KEY_LEN-1),直到遇到'='为止，不包括'='
        // [^=]:表示匹配所有不是'='的字符
        // %255s:读取一个字符串，最多读取255个字符，存入value
        // %s会从当前位置读取直到遇到空白字符(空格,制表符,换行)为止
        if (sscanf(line, "%63[^=]=%255s", key, value) != 2)
            continue;
        trim_whitespace(key);
        trim_whitespace(value);

        if (strcmp(key, "ip") == 0) {
            strncpy(config.ip, value, sizeof(config.ip) - 1);
        } else if (strcmp(key, "control_port") == 0) {
            config.control_port = atoi(value);
        } else if (strcmp(key, "data_port") == 0) {
            config.data_port = atoi(value);
        } else if (strcmp(key, "max_connections") == 0) {
            config.max_connections = atoi(value);
        } else if (strcmp(key, "upload_dir") == 0) {
            strncpy(config.upload_dir, value, sizeof(config.upload_dir) - 1);
        } else if (strcmp(key, "thread_pool_size") == 0) {
            config.thread_pool_size = atoi(value);
        } else if (strcmp(key, "task_queue_size") == 0) {
            config.task_queue_size = atoi(value);
        } else if (strcmp(key, "db_host") == 0) {
            strncpy(config.db_host, value, sizeof(config.db_host) - 1);
        } else if (strcmp(key, "db_user") == 0) {
            strncpy(config.db_user, value, sizeof(config.db_user) - 1);
        } else if (strcmp(key, "db_pass") == 0) {
            strncpy(config.db_pass, value, sizeof(config.db_pass) - 1);
        } else if (strcmp(key, "db_name") == 0) {
            strncpy(config.db_name, value, sizeof(config.db_name) - 1);
        }
    }

    fclose(fp);
    CLOUDISK_LOG_INFO("config file loaded");
    return 0;
}

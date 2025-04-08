#ifndef CONF_H
#define CONF_H

#include <cloudisk.h>

#define MAX_CONFIG_LINE 256 // 存储从配置文件读取的一行文本的数组的长度
#define IP_LEN 64 // IP地址数组长度
#define DIR_LEN 256 // 目录数组长度
#define KEY_LEN 64 // key数组的长度
#define VALUE_LEN 256 // value数组的长度

#define DB_HOST_LEN 64
#define DB_USER_LEN 64
#define DB_PASS_LEN 64
#define DB_NAME_LEN 64

typedef struct{
    char ip[IP_LEN]; // 服务器绑定的IP地址
    int control_port; // 控制连接端口
    int data_port; // 数据连接端口
    int max_connections; // 最大并发连接数 
    char upload_dir[DIR_LEN]; // 上传文件存储目录
    int thread_pool_size; // 线程池最大线程数
    int task_queue_size; // 任务队列数量
    char db_host[DB_HOST_LEN];  // 数据库服务器地址
    char db_user[DB_USER_LEN];  // 数据库用户名
    char db_pass[DB_PASS_LEN];  // 数据库密码
    char db_name[DB_NAME_LEN];  // 数据库名称
}ServerConfig;

// 声明config为全局变量，其他模块可以直接使用
extern ServerConfig config;

// 加载配置文件
// file_name:配置文件路径
// config:配置结构体指针，填充后返回配置信息
int load_config(const char *conf_name);

#endif

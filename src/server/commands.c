#include "commands.h"
// 此模块是对简单命令的处理

// 封装获取家目录的函数
char *get_home_directory() {
    // 从环境变量获取HOME
    char *home_dir = getenv("HOME");
    home_dir = getenv("HOME");
    if (home_dir == NULL) {
        // 环境变量不存在，使用getpwuid获取主目录
        struct passwd *pw = getpwuid(getuid());
        home_dir = pw->pw_dir;
    }
    // 关于返回值的问题：
    // getenv返回的字符串指针指向环境变量表中的静态数据 （由 libc 维护）
    // 这些数据在程序启动时初始化，内存生命周期与程序一致。
    // getpwuid 返回的struct passwd *pw指向的是一个静态分配的结构体
    // 生命周期是全局的，因此get_home_directory函数结束并不会消失
    return home_dir;
}

// 处理cd命令
int handle_cd(UserInfo *user, char *dir) {

    char resolved_path[PATH_MAX]; // 转换后的绝对路径
    char *target_dir = dir;
    char combined_path[PATH_MAX + 1] = {0};

    // 获取家目录
    if (dir == NULL || strcmp(dir, "") == 0 || strcmp(dir, "~") == 0 || dir[0] == '~') {
        if ((target_dir = get_home_directory()) == NULL) {
            send_msg(user->control_sockfd, "ERROR: Cannot determine home directory\n");
            CLOUDISK_LOG_ERR("%s: ERROR: Cannot determine home directory",user->username);
            return -1;
        }
    }

    // 处理不同路径情况
    if (strcmp(dir, "~") == 0) {
        // 直接使用家目录，无需拼接
        snprintf(combined_path, sizeof(combined_path), "%s", target_dir);
    } else if (dir[0] == '~' && dir[1] == '/') { // 如果是cd ~/目录形式
        snprintf(combined_path, sizeof(combined_path), "%s/%s", target_dir, dir + 2);
    } else if (dir[0] == '~') {
        // 无效的格式
        send_msg(user->control_sockfd, "Invalid argument\n");
        CLOUDISK_LOG_INFO("%s: Invalid argument",user->username);
    } else if (target_dir[0] != '/') {
        // 如果目标路径是相对路径，则将其与当前用户的虚拟工作目录拼接
        snprintf(combined_path, sizeof(combined_path), "%s/%s", user->current_dir, target_dir);
    } else {
        // 绝对路径直接使用
        strncpy(combined_path, target_dir, PATH_MAX);
    }

    // 使用realpath得到绝对路径 char *realpath(const char *path, char *resolved_path);
    if (realpath(combined_path, resolved_path) == NULL) {
        send_err_msg(user->control_sockfd, combined_path);
        CLOUDISK_LOG_ERROR_CHECK("realpath");
        return -1;
    }

    // 检查解析后的路径是否是有效目录
    struct stat st;
    // 必须先有stat的成功执行，确认路径有效，再去用S_ISDIR判断是否是目录
    if (stat(resolved_path, &st) != 0 || S_ISDIR(st.st_mode) == 0) {
        send_err_msg(user->control_sockfd, combined_path);
         CLOUDISK_LOG_ERROR_CHECK("stat");
        return -1;
    }

    // 更新当前用户的虚拟工作目录，不调用chdir改变全局目录
    strncpy(user->current_dir, resolved_path, PATH_MAX - 1);
    user->current_dir[PATH_MAX - 1] = '\0';
    send_msg(user->control_sockfd, "OK: Directory changed\n");
    CLOUDISK_LOG_INFO("%s: OK: Directory changed",user->username);

    return 0;
}

// 处理pwd命令的函数
int handle_pwd(UserInfo *user) {

    char cwd[PATH_MAX + 1]; // 存放当前目录的路径
    // 直接使用 user->current_dir 作为当前目录
    snprintf(cwd, sizeof(cwd), "%s\n", user->current_dir);
    // 发送pwd响应给客户端
    send_msg(user->control_sockfd, cwd);
    CLOUDISK_LOG_INFO("%s: Get the current directory successfully",user->username);

    return 0;
}

// mkdir
int handle_mkdir(UserInfo *user, char *dir) {
    // 构建目录路径
    char dir_path[PATH_MAX + 1] = {0};
    if (dir[0] == '~' && dir[1] == '/') {
        // 如果以~/开头
        char *home_dir = get_home_directory();
        if (home_dir != NULL) {
            snprintf(dir_path, sizeof(dir_path), "%s/%s", home_dir, dir + 2); // 跳过~和/
        } else {
            send_msg(user->control_sockfd, "ERROR: Cannot determine home directory\n");
            CLOUDISK_LOG_INFO("%s: Cannot determine home directory",user->username);
            return -1;
        }
    } else if (dir[0] == '/') {
        // 绝对路径直接使用
        strncpy(dir_path, dir, sizeof(dir_path) - 1);
    } else {
        // 相对路径，拼接
        snprintf(dir_path, sizeof(dir_path), "%s/%s", user->current_dir, dir);
    }

    if (mkdir(dir_path, 0775) != 0) {
        send_err_msg(user->control_sockfd, "mkdir");
        CLOUDISK_LOG_ERROR_CHECK("mkdir");
        return -1;
    }
    send_msg(user->control_sockfd, "OK: Directory created\n");
    CLOUDISK_LOG_INFO("%s: OK: Directory created",user->username);

    return 0;
}

// rmdir
int handle_rmdir(UserInfo *user, char *dir) {
    // 构建目录路径
    char dir_path[PATH_MAX + 1] = {0};
    if (dir[0] == '~' && dir[1] == '/') {
        // 如果以~/开头
        char *home_dir = get_home_directory();
        if (home_dir != NULL) {
            snprintf(dir_path, sizeof(dir_path), "%s/%s", home_dir, dir + 2); // 跳过~和/
        } else {
            send_msg(user->control_sockfd, "ERROR: Cannot determine home directory\n");
            CLOUDISK_LOG_INFO("%s: Cannot determine home directory",user->username);
            return -1;
        }
    } else if (dir[0] == '/') {
        // 绝对路径直接使用
        strncpy(dir_path, dir, sizeof(dir_path) - 1);
    } else {
        // 相对路径，拼接
        snprintf(dir_path, sizeof(dir_path), "%s/%s", user->current_dir, dir);
    }

    if (rmdir(dir_path) != 0) {
        send_err_msg(user->control_sockfd, "rmdir");
        CLOUDISK_LOG_ERROR_CHECK("rmdir");
        return -1;
    }
    send_msg(user->control_sockfd, "OK: Directory removed\n");
    CLOUDISK_LOG_INFO("%s: OK: Directory removed",user->username);

    return 0;
}

// rm
int handle_rm(UserInfo *user, char *file) {
    // 构建文件路径
    char file_path[PATH_MAX + 1] = {0};
    if (file[0] == '~' && file[1] == '/') {
        // 如果以~/开头
        char *home_dir = get_home_directory();
        if (home_dir != NULL) {
            snprintf(file_path, sizeof(file_path), "%s/%s", home_dir, file + 2); // 跳过~和/
        } else {
            send_msg(user->control_sockfd, "ERROR: Cannot determine home directory\n");
            CLOUDISK_LOG_INFO("%s: Cannot determine home directory",user->username);
            return -1;
        }
    } else if (file[0] == '/') {
        // 绝对路径直接使用
        strncpy(file_path, file, sizeof(file_path) - 1);
    } else {
        // 相对路径，拼接
        snprintf(file_path, sizeof(file_path), "%s/%s", user->current_dir, file);
    }

    if (unlink(file_path) != 0) {
        send_err_msg(user->control_sockfd, "rm");
        CLOUDISK_LOG_ERROR_CHECK("rm");
        return -1;
    }
    send_msg(user->control_sockfd, "OK: File removed\n");
    CLOUDISK_LOG_INFO("%s: OK: File removed",user->username);

    return 0;
}

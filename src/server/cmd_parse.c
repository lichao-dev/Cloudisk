#include "cmd_parse.h"

// 命令解析,并调用相关处理函数
int cmd_parse(UserInfo *user, char *cmd) {
    CLOUDISK_LOG_INFO("Received command from user %s: %s", user->username, cmd);
    char *tokens[TOKEN_NUM] = {NULL}; // 存放分解的token，token包括命令、参数
    int count = 0;                    //参数数量
    
    // strtok的返回值是token的指针，如果没有更多token返回NULL
    char *token = strtok(cmd, " \t\n");

    while (token != NULL && count < TOKEN_NUM) {
        tokens[count++] = token;
        // 传入NULL是告诉strtok继续从上一次结束的地方扫描
        token = strtok(NULL, " \t\n");
    }

    if (count == 0) {
        // 空命令，不做处理
        return 0;
    }

    // 处理cd命令
    if (strcmp(tokens[0], "cd") == 0) {
        if (count > 2) {
            char err_msg[MSG_LEN] = "cd: too many arguments\n";
            CLOUDISK_LOG_INFO("%s: cd: too many arguments",user->username);
            send_msg(user->control_sockfd, err_msg);
            return -1;
        } else if (count == 1) {
            // 如果没有参数，则认为切换到家目录
            handle_cd(user, "");
        } else {
            // count=2
            handle_cd(user, tokens[1]);
        }
        return 0;
    }

    // 处理pwd命令
    if (strcmp(tokens[0], "pwd") == 0) {
        handle_pwd(user);
        return 0;
    }

    // 处理ls命令
    if (strcmp(tokens[0], "ls") == 0) {
        handle_ls(user, tokens[1]);
        return 0;
    }

    // 处理gets命令
    if (strcmp(tokens[0], "gets") == 0) {
        if (count != 2) { // 如果参数数量不符合
            send_msg(user->control_sockfd, "ERROR: Invalid arguments for gets\n");
            CLOUDISK_LOG_INFO("%s: Invalid arguments for gets",user->username);
            return -1;
        }
        // 申请堆空间存放要传入handle_gets的参数
        FileTransferArg *arg = (FileTransferArg *)calloc(1, sizeof(FileTransferArg));
        if (arg == NULL) {
            perror("calloc in cmd_parse");
            send_msg(user->control_sockfd, "calloc failed in cmd_parse\n");
            CLOUDISK_LOG_ERR("%s: calloc failed in cmd_parse",user->username);
            return -1;
        }

        // 设置hanle_gets的参数
        arg->user = *user;
        strncpy(arg->filename, tokens[1], NAME_MAX);
        arg->filename[NAME_MAX] = '\0'; // 确保filename以\0结尾

        // 提交下载任务到线程池
        if ((thread_pool_add_task(handle_gets, arg)) < 0) {
            free(arg); // 提交失败，则释放内存
            send_msg(user->control_sockfd, "ERROR: Failed to add task to thread pool\n");
            CLOUDISK_LOG_ERR("%s: Failed to add task to thread pool",user->username);
            return -1;
        }

        return 0;
    }

    // 处理puts命令
    if (strcmp(tokens[0], "puts") == 0) {
        if (count != 2) { // 如果参数数量不符合
            send_msg(user->control_sockfd, "ERROR: Invalid arguments for puts\n");
            CLOUDISK_LOG_INFO("%s: Invalid arguments for gets",user->username);
            return -1;
        }
        // 申请堆空间存放要传入handle_puts的参数
        FileTransferArg *arg = (FileTransferArg *)calloc(1, sizeof(FileTransferArg));
        if (arg == NULL) {
            perror("calloc in cmd_parse");
            send_msg(user->control_sockfd, "calloc failed in cmd_parse\n");
            CLOUDISK_LOG_ERR("%s: calloc failed in cmd_parse",user->username);
            return -1;
        }

        // 设置hanle_puts的参数
        arg->user = *user;
        strncpy(arg->filename, tokens[1], NAME_MAX);
        arg->filename[NAME_MAX] = '\0';

        // 提交上传任务到线程池
        if ((thread_pool_add_task(handle_puts, arg)) < 0) {
            free(arg); // 提交失败，则释放内存
            send_msg(user->control_sockfd, "ERROR: Failed to add task to thread pool\n");
            CLOUDISK_LOG_ERR("%s: Failed to add task to thread pool",user->username);
            return -1;
        }

        return 0;
    }

    // mkdir
    if (strcmp(tokens[0], "mkdir") == 0) {
        if (count < 2) {
            send_msg(user->control_sockfd, "mkdir: missing operand\n");
            CLOUDISK_LOG_INFO("%s: mkdir: missing operand",user->username);
            return -1;
        }
        if (count > 2) {
            send_msg(user->control_sockfd, "ERROR: Invalid arguments for mkdir\n");
            CLOUDISK_LOG_INFO("%s: Invalid arguments for mkdir",user->username);
            return -1;
        }
        handle_mkdir(user, tokens[1]);
        return 0;
    }

    // rmdir
    if (strcmp(tokens[0], "rmdir") == 0) {
        if (count < 2) {
            send_msg(user->control_sockfd, "rmdir: missing operand\n");
            CLOUDISK_LOG_INFO("%s: rmdir: missing operand",user->username);
            return -1;
        }
        if (count > 2) {
            send_msg(user->control_sockfd, "ERROR: Invalid arguments for rmdir\n");
            CLOUDISK_LOG_INFO("%s: Invalid arguments for rmdir",user->username);
            return -1;
        }
        handle_rmdir(user, tokens[1]);
        return 0;
    }

    // rm删除
    if (strcmp(tokens[0], "rm") == 0) {
        if (count < 2) {
            send_msg(user->control_sockfd, "rm: missing operand\n");
             CLOUDISK_LOG_INFO("%s: rm: missing operand",user->username);
            return -1;
        }
        if (count > 2) {
            send_msg(user->control_sockfd, "ERROR: Invalid arguments for rm\n");
             CLOUDISK_LOG_INFO("%s: Invalid arguments for rm",user->username);
            return -1;
        }
        handle_rm(user,tokens[1]);
        return 0;
    }

    // 非法命令
    char buf[CMD_LEN];
    snprintf(buf, CMD_LEN, "%s: command not found\n", tokens[0]);
    CLOUDISK_LOG_INFO("%s: %s: command not found",user->username,tokens[0]);
    send_msg(user->control_sockfd, buf);

    return -1;
}

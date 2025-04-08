.SUFFIXES:
# 服务端和客户端源代码目录
SERVER_SRCDIR = src/server/
CLIENT_SRCDIR = src/client/
INCLUDE_DIR = include/

# 查找源文件
SERVER_SRCS = $(wildcard $(SERVER_SRCDIR)*.c)
CLIENT_SRCS = $(wildcard $(CLIENT_SRCDIR)*.c)

# 生成的目标文件和可执行文件放置目录
OBJDIR = bin/obj/
BINDIR = bin/

# 目标文件存放目录
SERVER_OBJDIR = $(OBJDIR)server/
CLIENT_OBJDIR = $(OBJDIR)client/

# 生成目标文件路径
SERVER_OBJS = $(patsubst $(SERVER_SRCDIR)%.c,$(SERVER_OBJDIR)%.o,$(SERVER_SRCS))
CLIENT_OBJS = $(patsubst $(CLIENT_SRCDIR)%.c,$(CLIENT_OBJDIR)%.o,$(CLIENT_SRCS))

# 可执行文件路径
SERVER_BIN = $(BINDIR)server
CLIENT_BIN = $(BINDIR)client

# 编译和链接选项
CC = gcc
CFLAGS = -Wall -g -I$(INCLUDE_DIR)  # 仍需 -Iinclude，因为 server.h 在这里
LDFLAGS = -pthread -lcrypt -lssl -lcrypto -lmysqlclient

.PHONY: all clean rebuild depend

# 默认目标
all: depend server client

# 服务端编译
server: $(SERVER_BIN)
client: $(CLIENT_BIN)

$(SERVER_BIN): $(SERVER_OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# 通用编译规则
# 依赖 include/%.h，但不直接依赖 /usr/include/my_header.h（由 gcc -MM 处理）
$(SERVER_OBJDIR)%.o: $(SERVER_SRCDIR)%.c | $(SERVER_OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(CLIENT_OBJDIR)%.o: $(CLIENT_SRCDIR)%.c | $(CLIENT_OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# 创建目录
$(BINDIR):
	mkdir -p $(BINDIR)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(SERVER_OBJDIR): | $(OBJDIR)
	mkdir -p $(SERVER_OBJDIR)

$(CLIENT_OBJDIR): | $(OBJDIR)
	mkdir -p $(CLIENT_OBJDIR)

# 确保 include/ 目录存在，因为 server.h 等头文件在这里
$(INCLUDE_DIR):
	mkdir -p $(INCLUDE_DIR)

# 自动生成依赖关系（包括 /usr/include/my_header.h）
depend: .depend

.depend: $(SERVER_SRCS) $(CLIENT_SRCS)
	$(CC) $(CFLAGS) -MM $^ -MF .depend

-include .depend

# 清理
clean:
	$(RM) -r $(OBJDIR) $(BINDIR) .depend

# 重构
rebuild: clean all

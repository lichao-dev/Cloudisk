OUT := main
SRCS := $(wildcard *.c) #将当前目录下的所有.c文件的文件名以空格分割，然后赋值给SRCS变量
OBJS := $(patsubst %.c,%.o,$(SRCS)) #获取当前目录下所有.c文件对应的.o文件，以空格分割
CFLAGS := -Wall -g
CC := gcc

$(OUT):$(OBJS)
	$(CC) $^ -o $@

%.o : %.c my_head.h #这里替换为自己定义的头文件
	$(CC) -c $< -o $@ $(CFLAGS)

.PHONY: clean rebuild

clean:
	$(RM) $(OUT) $(OBJS)

rebuild: clean $(OUT)



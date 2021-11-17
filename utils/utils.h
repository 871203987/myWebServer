#ifndef UTILS_H
#define UTILS_H

#include <sys/epoll.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>              //memset相关
#include <assert.h>
#include <errno.h>              //错误相关
#include <iostream>

class utils{

public:
    utils();
    ~utils();

public:
    static int pipefd;

    //工具函数
    static int setnonblocking(int fd);
    static void addfd(int epollfd, int fd, bool one_shot, int ET_enable);
    static void removefd(int epollfd, int fd);
    static void modifyfd(int epollfd, int fd, int eventType, int ET_enable);

    static void sig_handler( int sig);
    static void aadsig(int sig, void(handler)(int), bool restart);

    static void show_error(int connfd, const char *info);

};

#endif
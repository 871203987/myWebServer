#ifndef CONFIG_H
#define CONFIG_H

#include <unistd.h> 
#include <stdlib.h>
#include <string>

class config{

public:

    config();
    ~config();

    void parse_arg(int argc, char*argv[]);//解析main函数传入的参数

    //端口号
    int PORT;
    
    //是否关闭日志
    int close_log;

    //日志写入方式
    int LOGWrite;

    //日志路径
    std::string log_dirname;

    //触发组合模式
    int TRIGMode;

    //listenfd触发模式
    int LISTENTrigmode;

    //connfd触发模式
    int CONNTrigmode;

    //优雅关闭链接
    int OPT_LINGER;

    //数据库连接池数量
    int sql_num;

    //线程池内的线程数量
    int thread_num;

    //并发模型选择
    int actor_model;

};


#endif
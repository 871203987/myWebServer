#ifndef LOG_H
#define LOG_H

#include <stdlib.h>
#include <string>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdarg.h> //不定参数函数相关
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>


#include "../locker/locker.h"

using namespace std;


template<class T>
class block_queue{

public:
    block_queue(int max_size = 1000);
    ~block_queue();

    bool empty();
    bool full();
    bool push(const T &str);//插入队尾
    bool pop(T &str);//删除队首，并赋值给str
    bool front(T &str);//查队首，赋值给str
    bool back(T &str);//查队尾，赋值给str
    void clear();//清空

    int maxsize();
    int size();

private:
    T* m_logarray;//循环数组

    int m_maxsize;//最大条数
    int m_cursize;//当前条数

    int m_front;//队列头
    int m_back;//队列尾

    locker m_locker;
    cond m_cond;

};


class log{

public:
    static log* m_log;

    static locker m_locker;//静态锁，是由于静态函数只能访问静态成员,实例化对象时要加锁

private:

    char dir_name[128]; //路径名
    char log_name[128]; //log文件名
    int m_split_lines;  //日志最大行数
    int m_log_buf_size; //日志缓冲区大小
    long long m_count;  //日志行数记录
    int m_today;        //因为按天分类,记录当前时间是那一天
    FILE *m_fp;         //打开log的文件指针
    char *m_buf;        //要输出的内容

    block_queue<string> *m_log_queue; //阻塞队列
    bool m_is_async;                  //是否同步标志位
    int m_closeLog; //关闭日志



public:
    static log* getInstance();

    //可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init_log(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    static void *flush_log_thread(void *args);

    void write_log(int level, const char *format, ...);

    void flush(void);
    
    int get_logclose();

private:
    log();
    ~log();

    void* async_write_log();//异步写到文件

};

//这四个宏定义在其他文件中使用，主要用于不同类型的日志输出
#define LOG_DEBUG(format, ...) if(0 == log::getInstance()->get_logclose()) {log::getInstance()->write_log(0, format, ##__VA_ARGS__); log::getInstance()->flush();}
#define LOG_INFO(format, ...) if(0 == log::getInstance()->get_logclose()) {log::getInstance()->write_log(1, format, ##__VA_ARGS__); log::getInstance()->flush();}
#define LOG_WARN(format, ...) if(0 == log::getInstance()->get_logclose()) {log::getInstance()->write_log(2, format, ##__VA_ARGS__); log::getInstance()->flush();}
#define LOG_ERROR(format, ...) if(0 == log::getInstance()->get_logclose()) {log::getInstance()->write_log(3, format, ##__VA_ARGS__); log::getInstance()->flush();}

#endif
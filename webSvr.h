#ifndef WEBSVR_H
#define WEBSVR_H

#include <sys/epoll.h>          //epoll相关
#include <sys/socket.h>         //socket相关
#include <assert.h>             //assert相关
#include <fcntl.h>              //句柄操作相关
#include <arpa/inet.h>          //地址相关
#include <errno.h>              //错误相关
#include <unistd.h>             //close相关
#include<signal.h>              //信号相关


#include "utils/utils.h"              //公共函数
#include "http_conn/http_conn.h"          //客户端连接类
#include "timer/timer.h"         //定时器类
#include "mysql_pool/mysql_pool.h"  //数据库
#include "log/log.h"  //日志
#include "thread_pool/thread_pool.h"  //线程池




using namespace std;

const int MAX_FD = 65536;           //最大文件描述符
const int MAX_EVENT_NUMBER = 10000; //最大事件数
const int TIMESLOT = 5;             //定时器最小超时单位

class webSvr{

public:
    //构造函数、析构函数
    webSvr();
    ~webSvr();

    void init(int port, int trigmode, int actormode,string user, string passWord, string databaseName, int sql_num,
                int log, int logmode, string logdirname, int threadNums);         //初始化

    //socket、epoll监听相关
    void eventListen();                 //创建监听socket，并注册epoll事件
    void eventLoop();                   //循环监听新连接，读写事件等

    void trigmode_ET();                 //初始化监听socket和连接的触发模式，LT、ET

    bool dealwithClientConnect();            //处理新连接
    void dealwithRead(int sockfd);            //处理读事件
    void dealwithWrite(int sockfd);            //处理写事件
    bool dealwithSignal(bool &timeout, bool &stop_server);            //处理信号

    //accept之后新的http连接候，初始化该连接（注册进内核、加定时器）
    void init_conn_timer(int connfd, struct sockaddr_in client_address, int ET_enable, char* root);
    void adjust_conn_timer(timer *tim);
    void remove_conn_timer(timer *tim, int sockfd );

    //数据库相关
    void sql_pool();

    //日志相关
    void log_write();

    //线程池相关
    void init_thread_pool();

//变量
public:

    //基础
    int m_port;                              //监听端口
    char* m_root;

    //socket、epoll监听相关
    int m_listenfd;                         //监听socket句柄
    int m_epollfd;                          //epoll句柄
    epoll_event events[MAX_EVENT_NUMBER];      //事件数组
    int m_pipefd[2];                             //管道，传输信号

    //epoll事件触发模式
    int m_trigeMode;
    int m_listenET_enable;
    int m_connectET_enable;

    //客户端连接数组、及响应的定时器数据
    http_conn* clients;   //不能直接用数组
    client_data* clients_timer;   //用户定时器数组
    timer_list m_timerList;   //定时器链表

    //并发模型选择
    int m_actorMode;          //0proactor，1reactor

    //数据库相关
    mysql_pool *m_connPool;
    string m_user;         //登陆数据库用户名
    string m_passWord;     //登陆数据库密码
    string m_databaseName; //使用数据库名
    int m_sql_num;   //数据库连接数量

    //日志相关
    int m_closeLog;             //是否打开日志
    int m_logMode;             //日志同步or异步
    string m_logDirname;         //日志存储路径

    //线程池相关
    thread_pool<http_conn> *m_threadpool;
    int m_threadNums;
    int m_maxRequestNums;
    int m_isReactorMode;

};
#endif
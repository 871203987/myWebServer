#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include<iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <map>


#include "../utils/utils.h"
#include "../mysql_pool/mysql_pool.h"
#include "../locker/locker.h"
#include "../log/log.h"

class http_conn{

public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;

    static int m_epollfd;           //epoll句柄
    static int m_client_count;    //连接数量

    //标志变量
    int isdeal;              //请求是否被处理，被工作线程修改
    int istimeout;              //定时器是否要删掉（读取失败或者写入失败），被工作线程修改。因为http类中没有引入timer，所以需要一个标志位让主线程判断，然后删除timer

    int m_state;  //读为0, 写为1

    // 状态机状态
    enum CHECK_STATE{                 //主状态机状态
        CHECK_STATE_REQUESTLINE = 0,  //解析请求行
        CHECK_STATE_HEADER,          //解析请求头部
        CHECK_STATE_CONTENT        //解析内容
    };
    enum LINE_STATE{             //从状态机状态
        LINE_OK = 0,             //完整行
        LINE_BAD,             //行出错
        LINE_OPEN             //行不完整
    };

    //http请求方法
    enum METHOD{
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };

    //报文解析的结果
    enum HTTP_CODE{
        NO_REQUEST,               //请求不完整，需要继续读取请求报文数据
        GET_REQUEST,            //获得了完整的HTTP请求
        BAD_REQUEST,            //HTTP请求报文有语法错误
        NO_RESOURCE,            //没有资源可以返回
        FORBIDDEN_REQUEST,            //服务器拒绝该次访问
        FILE_REQUEST,            //文件请求
        INTERNAL_ERROR,            //服务器内部错误
        CLOSED_CONNECTION            //关闭连接
    };


private:
    //连接相关
    int m_sockfd;                //连接句柄
    sockaddr_in m_addr;           //连接地址
    int m_connectET_enable;       //连接的触发模式（ET or LT）

    //读相关
    char m_readBuff[READ_BUFFER_SIZE];
    int m_readIndex;         //缓冲区中m_read_buf中数据的最后一个字节的下一个位置,即数据大小
    int m_checkIndex;         //m_read_buf读取的位置
    int m_start_line;         //m_read_buf中已经解析的字符个数

    int m_cgi;

    char* m_url;
    METHOD m_method;
    char* m_version;

    char* m_host;
    bool m_linger;          //是否是长连接，keep-alive
    int m_contentLength;
    char m_contentType[200];//传输类型

    char* m_postInfo;

    CHECK_STATE m_check_state; //主状态机状态
    LINE_STATE m_line_state;//从状态机状态

    //写相关
    char* m_doc_root;             //文件根目录
    char m_real_url[FILENAME_LEN];           //文件真实路径，=m_doc_root+m_url
    char* m_fileAddress;
    struct stat m_fileStatus;


    char m_writeBuff[WRITE_BUFFER_SIZE];
    int m_writeIndex;

    struct iovec m_iv[2];
    int m_ivCount;

    int m_bytesToSend;
    int m_bytesHaveSend;

    //数据库相关
    MYSQL* m_mysql;
    // char sql_user[100];
    // char sql_passwd[100];
    // char sql_name[100];
    static map<string,string> users;              //用户的 用户名与密码
    locker m_locker;
    static mysql_pool* connpool;




public:
    http_conn();
    ~http_conn();

    //初始化连接、关闭连接
    void init_coon(int sockfd, const sockaddr_in &c_addr, int connectET_enable,char* doc_root);
    void close_conn(bool real_close=true);
    void init_sql(mysql_pool *sql_pool);

    //读相关
    bool read_once();//将数据从内核缓存区读到用户缓存区，因为proactor模式中要先读，故不设置为私有

    //写相关
    bool write();//将数据从用户缓存区读到内核缓存区，因为proactor模式中要先读，故不设置为私有

    //总的处理过程
    void process();

    sockaddr_in *get_address();
    int get_sockfd();

private:
    void init();

    //解析相关
    HTTP_CODE process_read();//主从状态机处理过程函数

    HTTP_CODE parse_requestLine(char *text);//主状态机状态函数-处理请求行
    HTTP_CODE parse_header(char *text);//主状态机状态函数-处理头部信息
    HTTP_CODE parse_content(char *text);//主状态机状态函数-处理数据内容,POST

    LINE_STATE parse_line();//从状态机状态函数-处理一行

    //处理相关
    HTTP_CODE do_request();//登录注册、或者返回资源

    //写相关
    bool process_write(HTTP_CODE read_res);

    bool add_line(const char *formate, ...);

    bool add_stausLine(int status, const char *title);

    bool add_linger();
    bool add_contentType();
    bool add_contentLength(int contentLength);
    bool add_blankLine();
    bool add_header(int contentLength);

    bool add_content(const char *content);

    void unmap();

};

#endif
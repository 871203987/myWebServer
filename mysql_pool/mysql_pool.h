#ifndef MYSQL_POOL_H
#define MYSQL_POOL_H

#include <mysql/mysql.h>
#include <string>
#include <list>
#include <iostream>

#include "../locker/locker.h"
#include "../log/log.h"


using namespace std;

class mysql_pool{

public:
    string m_host;         //主机地址
    string m_port;         //MySQL端口号
    string m_user;         //用户名
    string m_password;     //密码
    string m_dbName;         //数据库名

private:
    locker lock;         //锁
    sem sem_freeConn_count;//当前空闲连接的信号量

    int m_maxConn;           //最大连接数
    int m_busyConn;           //正在使用的连接数
    int m_freeConn;           //空闲的连接数

    list<MYSQL*> mysqlIDs;       //连接池


public:
    static mysql_pool* getInstanse(int maxConn);               //饿汉单例模式

    void init(string host, int port, string user, string password, string dbName);

    void destoryPool();                  //销毁连接池内的所有连接
    
    MYSQL* getConnect();               //从连接池内获取一个连接
    bool freeConnect(MYSQL* con);               //释放一个连接回连接池

    int getBusyConnect_count();             //获取正在使用的连接数
    int getFreeConnect_count();             //获取空闲的连接数

private:
    mysql_pool(int maxConn);
    ~mysql_pool();
};

class connectionRAII{
public:
    connectionRAII(MYSQL **conn, mysql_pool *mysql_pool);
	~connectionRAII();
private:
    MYSQL* connRALL;
    mysql_pool* poolRALL;
};


#endif
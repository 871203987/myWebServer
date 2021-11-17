#include "mysql_pool.h"


mysql_pool::mysql_pool(int maxConn){
    m_maxConn=maxConn;
    m_busyConn=0;
    m_freeConn=0;
}

mysql_pool::~mysql_pool(){

    destoryPool();

}

mysql_pool* mysql_pool::getInstanse(int maxConn){
    static mysql_pool connPool(maxConn);
	return &connPool;
};

void mysql_pool::init(string host, int port, string user, string password, string dbName){
    m_host = host;
    m_port = port;
    m_user = user;
    m_password = password;
    m_dbName = dbName;

    for(int i=0;i<m_maxConn;i++){
        MYSQL* con=NULL;
        con = mysql_init(con);
        if(con==NULL)
        {
			LOG_ERROR("MySQL Error");

            exit(1);
        }
        con = mysql_real_connect(con, host.c_str(), user.c_str(), password.c_str(), dbName.c_str(), port , NULL, 0);

        if(con==NULL)
        {
			LOG_ERROR("MySQL Error");

            exit(1);
        }
        mysqlIDs.push_back(con);
        m_freeConn++;
    }

    m_maxConn=m_freeConn;
    sem_freeConn_count = sem(m_freeConn);

}


void mysql_pool::destoryPool(){
    lock.lock();

    if(mysqlIDs.size()>0){
        list<MYSQL*>::iterator it;
        for(it=mysqlIDs.begin();it!=mysqlIDs.end() ;it++){
            MYSQL* con=*it;
            if(con!=NULL){
                mysql_close(con);
            }
        }
    }
    m_freeConn=0;
    m_busyConn=0;
    mysqlIDs.clear();

    lock.unlock();

};

MYSQL* mysql_pool::getConnect(){

    MYSQL* con = NULL;

    sem_freeConn_count.wait();

    lock.lock();

    con = mysqlIDs.front();
    mysqlIDs.pop_front();

    m_busyConn++;
    m_freeConn--;

    lock.unlock();

    return con;

};

bool mysql_pool::freeConnect(MYSQL* con){
    if(con == NULL){
        return false;
    }

    lock.lock();
    mysqlIDs.push_back(con);
    m_freeConn++;
    m_busyConn--;
    sem_freeConn_count.post();
    lock.unlock();
    return true;
}; 

int mysql_pool::getBusyConnect_count(){
    int busy_count=0;
    lock.lock();
    busy_count=m_busyConn;
    lock.unlock();
    return busy_count;
};             //获取正在使用的连接数
int mysql_pool::getFreeConnect_count(){
    int free_count=0;
    lock.lock();
    free_count=m_busyConn;
    lock.unlock();
    return free_count;
};             //获取空闲的连接数


connectionRAII::connectionRAII(MYSQL **conn, mysql_pool *mysql_pool){
    *conn = mysql_pool->getConnect();
    connRALL = *conn;
    poolRALL = mysql_pool;
};
connectionRAII::~connectionRAII(){
    poolRALL->freeConnect(connRALL);
};






#include "log.h"


template<class T>
block_queue<T>::block_queue(int max_size){
    if (max_size <= 0)
    {
        exit(-1);
    }
    m_maxsize = max_size;
    m_logarray = new T[m_maxsize];
    m_cursize = 0;
    m_front = -1;
    m_back = -1;
}

template<class T>
block_queue<T>::~block_queue(){
    m_locker.lock();
        if (m_logarray != NULL){
            delete[] m_logarray;
            m_logarray=NULL;
        }
    m_locker.unlock();
};

template<class T>
bool block_queue<T>::empty(){
    m_locker.lock();
        if (0 == m_cursize)
        {
            m_locker.unlock();
            return true;
        }
    m_locker.unlock();
    return false;
};

template<class T>
bool block_queue<T>::full(){
    m_locker.lock();
    if(m_cursize == m_maxsize){
        m_locker.unlock();
        return true;
    }
    m_locker.unlock();
    return false;
};

//往队列添加元素，需要将所有使用队列的线程先唤醒
//当有元素push进队列,相当于生产者生产了一个元素
//若当前没有线程等待条件变量,则唤醒无意义
template<class T>
bool block_queue<T>::push(const T &str){
    m_locker.lock();
    if (m_cursize >= m_maxsize)//队列满
    {
        m_cond.broadcast();//唤醒线程处理
        m_locker.unlock();
        return false;
    }
    m_back = (m_back + 1) % m_maxsize;
    m_logarray[m_back] = str;
    m_cursize++;
    m_cond.broadcast();//broadcast不需要锁
    m_locker.unlock();
    return true;
};//插入队尾

template<class T>
bool block_queue<T>::pop(T &str){
    m_locker.lock();
    //多个消费者的时候，这里要是用while而不是if
    while (m_cursize <= 0)
    {
        //当重新抢到互斥锁，pthread_cond_wait返回为0
        if (!m_cond.wait(m_locker.get()))
        {
            m_locker.unlock();
            return false;
        }
    }
    //取出队列首的元素，这里需要理解一下，使用循环数组模拟的队列 
    m_front = (m_front + 1) % m_maxsize;
    str = m_logarray[m_front];
    m_cursize--;
    m_locker.unlock();
    return true;
};//删除队首，并赋值给str

template<class T>
bool block_queue<T>::front(T &str){
    m_locker.lock();
    if (0 == m_cursize)
    {
        m_locker.unlock();
        return false;
    }
    str = m_logarray[m_front];
    m_locker.unlock();
    return true;
};//查队首，赋值给str

template<class T>
bool block_queue<T>::back(T &str){
    m_locker.lock();
    if (0 == m_cursize)
    {
        m_locker.unlock();
        return false;
    }
    str = m_logarray[m_back];
    m_locker.unlock();
    return true;
};//查队尾，赋值给str

template<class T>
void block_queue<T>::clear(){
    m_locker.lock();
        m_cursize = 0;
        m_front = -1;
        m_back = -1;
    m_locker.unlock();
};//清空

template<class T>
int block_queue<T>::maxsize(){
    int tem=0;
    m_locker.lock();
        tem = m_maxsize;
    m_locker.unlock();
    return tem;
};

template<class T>
int block_queue<T>::size(){
    int tem=0;
    m_locker.lock();
        tem = m_cursize;
    m_locker.unlock();
    return tem;
};




log* log::m_log=NULL;
locker log::m_locker;

log::log(){
    m_count = 0;
    m_is_async = false;
}

log::~log(){
    if (m_fp != NULL)
    {
        fclose(m_fp);
    }
}

log* log::getInstance(){//懒汉模式获取单例
    if(m_log==NULL){
        m_locker.lock();
        if(m_log==NULL){
            m_log=new log;
        }
        m_locker.unlock();
    }

    return m_log;
}

bool log::init_log(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size){
    //如果设置了max_queue_size,则设置为异步
    if (max_queue_size >= 1)
    {
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        //flush_log_thread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);//此线程一直在运行,消息队列本身是阻塞的，即flush_log_thread已经运行起来了
    }
    m_closeLog = close_log;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    //获取当前时间
    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    //从后往前找到第一个/的位置
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};
    //相当于自定义日志名
    //若输入的文件名没有/，则直接将时间+文件名作为日志名
    if (p == NULL)
    {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else
    {
        //将/的位置向后移动一个位置，然后复制到logname中
        //p - file_name + 1是文件所在路径文件夹的长度
        //dirname相当于./
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        dir_name[ p - file_name + 1]='\0';
        if(-1==access(dir_name,0)){
            if(mkdir(dir_name,S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)==-1){
                return false;
            }
        }
        //后面的参数跟format有关
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;

    m_fp = fopen(log_full_name, "a");//以追加的方式打开日志文件
    if (m_fp == NULL)
    {
        return false;
    }

    return true;

};

void* log::flush_log_thread(void *args){//异步写线程回调函数
    log::getInstance()->async_write_log();
}

void log::write_log(int level, const char *format, ...){//写日志，内部区分同步或者异步写
    
    struct timeval now = {0, 0};//获取当前时刻
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);//获取当前天
    struct tm my_tm = *sys_tm;
    char s[16] = {0};

    switch (level)//日志分级
    {
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }

    m_locker.lock();
    m_count++;//更新现有行数

    //日志不是今天或写入的日志行数是最大行的倍数,m_split_lines为最大行数
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) //everyday log
    {
        
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};
       
        //格式化日志名中的时间部分
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
        //如果是时间不是今天,则创建今天的日志，更新m_today和m_count
        if (m_today != my_tm.tm_mday)
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        //超过了最大行，在之前的日志名基础上加后缀, m_count/m_split_lines
        else
        {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }

    m_locker.unlock();

    va_list valst;//...不定参数列表
    va_start(valst, format);//format格式化输出形式

    string log_str;
    m_locker.lock();


    //写入的具体时间内容格式
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",//snprintf类似于printf功能，写入时间，日志登记等
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);//尾部追加日志信息
    m_buf[n + m] = '\n';//换行
    m_buf[n + m + 1] = '\0';//字符串结束标志
    log_str = m_buf;

    m_locker.unlock();

    if (m_is_async && !m_log_queue->full())//异步写
    {
        m_log_queue->push(log_str);//push内部有锁
    }
    else//同步写
    {
        m_locker.lock();
        fputs(log_str.c_str(), m_fp);
        m_locker.unlock();
    }

    va_end(valst);

};

void* log::async_write_log(){//异步线程写，初始化log时就已将在运行了，阻塞在pop这里，如果有新任务则被唤醒
    string single_log;
        //从阻塞队列中取出一个日志string，写入文件
    while (m_log_queue->pop(single_log))
    {
        m_locker.lock();
        fputs(single_log.c_str(), m_fp);
        m_locker.unlock();
    }
};

int log::get_logclose(){
    return m_closeLog;
}

void log::flush(void)
{
    m_locker.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_locker.unlock();
}
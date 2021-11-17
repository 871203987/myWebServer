#include <iostream>

#include "webSvr.h"

webSvr::webSvr(){
    clients = new http_conn[MAX_FD];//连接
    clients_timer = new client_data[MAX_FD];//定时器

    //root文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[13] = "/root/mysite";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

}

webSvr::~webSvr(){
    delete[] clients;
    delete[] clients_timer;
    delete m_threadpool;

    free(m_root);
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[0]);
    close(m_pipefd[1]);

    LOG_INFO("%s", "server stop!");

}

void webSvr::init(int port, int trigmode, int actormode, string user, string passWord, string databaseName, int sql_num, int log, int logmode, string logdirname, int threadNums){
    m_port=port;
    m_trigeMode=trigmode;
    m_actorMode=actormode;

    m_user = user;
    m_passWord = passWord;
    m_databaseName = databaseName;
    m_sql_num = sql_num;

    m_closeLog = log;
    m_logMode = logmode;
    m_logDirname=logdirname;

    m_threadNums=threadNums;
    
}


void webSvr::sql_pool(){
    //初始化数据库连接池
    m_connPool = mysql_pool::getInstanse(m_sql_num);
    m_connPool->init("localhost",3306,  m_user, m_passWord, m_databaseName);
    
    LOG_INFO("%s", "sql_pool start!");

    //初始化数据库读取表
    clients->init_sql(m_connPool);
}

void webSvr::log_write(){
    if (0 == m_closeLog)//0为打开
    {
        //初始化日志

        if (1 == m_logMode){
            log::getInstance()->init_log(m_logDirname.c_str(), m_closeLog, 2000, 800000, 800);
            LOG_INFO("%s", "asyn_log start!");
        }//1为异步
        else{
            log::getInstance()->init_log(m_logDirname.c_str(), m_closeLog, 2000, 800000, 0);
            LOG_INFO("%s", "syn_log start!");
        }
            
    }
};

void webSvr::init_thread_pool(){
    m_threadpool=new thread_pool<http_conn>(&m_timerList, clients_timer, m_actorMode,m_threadNums);
    LOG_INFO("%s", "thread_pool start!");
};
///////////////////////////////////
void webSvr::trigmode_ET(){
    if(m_trigeMode==0){             //LT+LT
        m_listenET_enable=0;
        m_connectET_enable=0;
        LOG_INFO("%s", "listenET_enable:LT     connectET_enable:LT");
    }
    else if(m_trigeMode==1){             //LT+ET
        m_listenET_enable=0;
        m_connectET_enable=1;
        LOG_INFO("%s", "listenET_enable:LT     connectET_enable:ET");
    }
    else if(m_trigeMode==2){             //ET+LT
        m_listenET_enable=1;
        m_connectET_enable=0;
        LOG_INFO("%s", "listenET_enable:ET     connectET_enable:LT");
    }
    else if(m_trigeMode==3){             //ET+ET
        m_listenET_enable=1;
        m_connectET_enable=1;
        LOG_INFO("%s", "listenET_enable:ET     connectET_enable:ET");
    }
}

void webSvr::init_conn_timer(int connfd, struct sockaddr_in client_address, int ET_enable, char* root){//在dealwithClientConnect()用到；
    clients[connfd].init_coon(connfd,client_address,ET_enable,root);
    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    clients_timer[connfd].sockfd=connfd;
    clients_timer[connfd].address = client_address;
    timer* tim = new timer;
    tim->c_data=&clients_timer[connfd];
    tim->callback_func = callback_func;
    time_t cur = time(NULL);
    tim->expireTime = cur + 3 * TIMESLOT;
    clients_timer[connfd].client_timer = tim;
    m_timerList.add_timer(tim);
};

void webSvr::adjust_conn_timer(timer *tim){
    time_t cur = time(NULL);
    tim->expireTime = cur + 3 * TIMESLOT;
    m_timerList.adjust_timer(tim);

    LOG_INFO("%s", "adjust timer once");
}

void webSvr::remove_conn_timer(timer *tim, int sockfd ){//一般为发生错误，主动关闭单个定时器，在定时检查中会批量关闭过期定时器
    // tim->callback_func(&clients_timer[sockfd]);//移除注册事件，关闭连接
    if (tim)
    {
        m_timerList.remove_timer(tim);//不仅删除了链表上的tim，也删除了自身的tim
    }
    LOG_INFO("close client(%s)", inet_ntoa(clients[sockfd].get_address()->sin_addr));
}

bool webSvr::dealwithClientConnect(){
    struct sockaddr_in c_addr;
    socklen_t c_addrLength = sizeof(c_addr);
    if(m_listenET_enable==0){
        int connfd = accept(m_listenfd, (struct sockaddr *)&c_addr, &c_addrLength);//accept连接
        
        if(connfd<0){                  //不能用assert()，因为不期望程序停止
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if(http_conn::m_client_count >= MAX_FD){
            utils::show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            close(connfd); 
            return false;
        }
        LOG_INFO("new client:%s",inet_ntoa(c_addr.sin_addr));
        init_conn_timer(connfd,c_addr,m_connectET_enable,m_root);//给连接加定时器，注册内核事件
    }
    else if(m_listenET_enable==1){
        while(1){
            int connfd = accept(m_listenfd, (struct sockaddr *)&c_addr, &c_addrLength);//accept连接
            if(connfd<0){     
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if(http_conn::m_client_count >= MAX_FD){
                utils::show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                close(connfd);
                break;
            }
            LOG_INFO("new client:%s",inet_ntoa(c_addr.sin_addr));
            init_conn_timer(connfd,c_addr,m_connectET_enable,m_root);//给连接加定时器，注册内核事件
        }
        return false;
    }
    return true;
};

void webSvr::dealwithRead(int sockfd){

    timer* tim = clients_timer[sockfd].client_timer;//获取定时器

    //reactor模式，读写交给工作线程去做。
    if(m_actorMode==1){
        if(tim!=NULL){
            adjust_conn_timer(tim);//更新定时器
        }
        LOG_INFO("deal with the client(%s)", inet_ntoa(clients[sockfd].get_address()->sin_addr));
        // if(clients[sockfd].read_once()){
        //     cout<<"here"<<endl;
        //     clients[sockfd].process();
        // }
        // else{
        //     remove_conn_timer(tim,sockfd);//移除定时器
        //     clients[sockfd].close_conn();//关闭连接
        // }
        m_threadpool->addTask(clients+sockfd,0);  //将请求添加到线程池消息队列,线程池会自动处理任务，处理失败则会修改定时器标志

        // while(true){
        //     if(clients[sockfd].isdeal==1){
        //         if(clients[sockfd].istimeout==1){
        //             remove_conn_timer(tim,sockfd);//移除定时器
        //             clients[sockfd].close_conn();//关闭连接
        //             clients[sockfd].istimeout=0;
        //         }
        //         clients[sockfd].isdeal=0;
        //         break;
        //     }
        // }
    }
    //proactor模式，读写由主线程完成。
    else if(m_actorMode== 0){
        if(clients[sockfd].read_once()){           //read_once读取浏览器端发送来的请求报文，直到无数据可读或对方关闭连接，读取到m_read_buffer中；
            LOG_INFO("deal with the client(%s)", inet_ntoa(clients[sockfd].get_address()->sin_addr));
            m_threadpool->addTask(clients+sockfd,0);  //将读完的请求添加到线程池消息队列
            // clients[sockfd].process();

            if(tim!=NULL){
                adjust_conn_timer(tim);//更新定时器
            }
        }
        else{
            remove_conn_timer(tim,sockfd);//移除定时器
            clients[sockfd].close_conn();//关闭连接
        }
    }
}

void webSvr::dealwithWrite(int sockfd){

    timer* tim = clients_timer[sockfd].client_timer;//获取定时器
    
    if(m_actorMode==1){
        
        if(tim!=NULL){
            adjust_conn_timer(tim);//更新定时器
        }
        // if(clients[sockfd].write()){
        //     LOG_INFO("send data to the client(%s)", inet_ntoa(clients[sockfd].get_address()->sin_addr));
        // }
        // else{
        //     remove_conn_timer(tim,sockfd);//移除定时器
        //     clients[sockfd].close_conn();//关闭连接 
        // };
        LOG_INFO("send data to the client(%s)", inet_ntoa(clients[sockfd].get_address()->sin_addr));
        m_threadpool->addTask(clients+sockfd,1);  //将请求添加到线程池消息队列
        // while(true){
        //     if(clients[sockfd].isdeal==1){
        //         if(clients[sockfd].istimeout==1){
        //             remove_conn_timer(tim,sockfd);//移除定时器
        //             clients[sockfd].close_conn();//关闭连接
        //             clients[sockfd].istimeout=0;            
        //         }
        //         clients[sockfd].isdeal=0;
        //         break;
        //     }
        // }
    }
    else if(m_actorMode==0){
        if(clients[sockfd].write()){           //主线程写数据；
            LOG_INFO("send data to the client(%s)", inet_ntoa(clients[sockfd].get_address()->sin_addr));
            if(tim!=NULL){
                adjust_conn_timer(tim);//更新定时器
            }
            // m_threadpool->addIssure(clients[sockfd]);  //不需要加入线程池了，因为写完任务就已经完成了，不需要再进行处理
        }
        else{
            remove_conn_timer(tim,sockfd);//移除定时器
            clients[sockfd].close_conn();//关闭连接 
        }
    }
}

bool webSvr::dealwithSignal(bool &timeout, bool &stop_server){
    int ret = 0;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true; 
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            case SIGINT:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

//创建监听socket
void webSvr::eventListen(){

    //创建socket
    struct sockaddr_in s_addr;
    m_listenfd = socket ( PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd>=0);

    int ret=0;

    //绑定socket
    s_addr.sin_family=AF_INET;
    s_addr.sin_addr.s_addr=htonl(INADDR_ANY);
    s_addr.sin_port=htons(m_port);
    ret=bind(m_listenfd, (struct sockaddr*)&s_addr, sizeof(s_addr));
    assert(ret>=0);

    //监听socket
    ret = listen(m_listenfd, 5);
    assert(ret>=0);

    //创建epoll内核事件表
    m_epollfd=epoll_create(5);
    assert(m_epollfd!=-1);
    http_conn::m_epollfd=m_epollfd;          //绑定连接类的epoll句柄（方便客户连接的注册）

    //往epoll中注册socket事件
    utils::addfd(m_epollfd, m_listenfd, false, m_listenET_enable);

    //创建全双工管道，1端写，0端读
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret!=-1);
    utils::addfd(m_epollfd, m_pipefd[0], false, 0);//将读端设为非阻塞，并注册事件

    utils::setnonblocking(m_pipefd[1]);//将写端设为非阻塞
    utils::pipefd = m_pipefd[1];
    utils::aadsig(SIGPIPE, SIG_IGN,true);//如果收到客户端关闭连接的信号，则忽略,不设置为关闭
    utils::aadsig(SIGALRM, utils::sig_handler,false);//处理SIGALRM信号，定时器
    utils::aadsig(SIGTERM, utils::sig_handler,false);//处理SIGTERM信号，关闭服务器
    utils::aadsig(SIGINT, utils::sig_handler,false);//处理SIGTERM信号，关闭服务器

    alarm(TIMESLOT);//开启定时
}

//进入循环，接收各种请求
void webSvr::eventLoop(){
    bool stop_server=false;
    bool timeout=false;//定时器定时任务
     
    int tick_count=0;

    while(!stop_server){
        int number=epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if(number<0&&errno!=EINTR){
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        for(int i=0; i< number; i++){
            int tem_fd = events[i].data.fd;
            //处理新的客户端连接
            if(tem_fd==m_listenfd){
                bool flag=dealwithClientConnect();
                if(false==flag){
                    continue;
                }
            }
            //处理读事件
            else if((tem_fd != m_pipefd[0])&&(events[i].events & EPOLLIN)){
                dealwithRead(tem_fd);
            }
            //处理读事件
            else if(events[i].events & EPOLLOUT){
                dealwithWrite(tem_fd);
            }
            //处理信号
            else if((tem_fd == m_pipefd[0]) && (events[i].events & EPOLLIN)){//管道传入，输入事件
                if(dealwithSignal(timeout, stop_server)==false){
                    LOG_ERROR("%s", "dealclientdata failure");
                }
            }
            //处理异常、关闭等
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                timer* tim = clients_timer[tem_fd].client_timer;
                remove_conn_timer(tim,tem_fd);//移除定时器
                clients[tem_fd].close_conn();//关闭连接
            }
        }
        if(timeout){
            m_timerList.tick();//删除过期定时器，关闭连接
            alarm(TIMESLOT);//重新定时
            LOG_INFO("%s", "timer tick");
            timeout=false;
        }
    }

}
/////////////////////////////////////
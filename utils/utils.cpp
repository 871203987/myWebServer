#include "utils.h"


int utils::pipefd = 0;

utils::utils(){};
utils::~utils(){};

//设置fd（socket）为非阻塞，即fd内的读写函数立即返回，不阻塞
int utils::setnonblocking(int fd){
    int old_option = fcntl (fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//往epoll内核事件表内注册写读事件，并设置触发模式，默认为LT
void utils::addfd(int epollfd, int fd, bool one_shot, int ET_enable){
    epoll_event event;
    event.data.fd=fd;
    event.events= EPOLLIN | EPOLLRDHUP;            //开启读事件、对方关闭连接事件
    if(ET_enable){
        event.events |= EPOLLET;      //开启ET高效模式，边缘触发，只触发一次，必须立即处理
    }
    if(one_shot){
        event.events |= EPOLLONESHOT; //针对某些fd，需要保证任意时刻该fd只能被一个线程处理
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking(fd);
}

//从epoll内核事件表内删除事件
void utils::removefd(int epollfd, int fd){
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0 );
}

//将事件重设为ONESHOT模式
void utils::modifyfd(int epollfd, int fd, int eventType, int ET_enable){
    epoll_event event;
    event.data.fd=fd;
    if(ET_enable==1){
        event.events= eventType | EPOLLET | EPOLLHUP | EPOLLONESHOT;
    }
    else if(ET_enable==0){
        event.events= eventType | EPOLLHUP | EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event );
}


void utils::sig_handler(int sig){//往管道里写信号
    int save_errno = errno;
    int msg = sig;
    send(pipefd, (char *)&msg, 1, 0);
    errno = save_errno;
}

void utils::aadsig(int sig, void(handler)(int), bool restart){//设置系统监听alarm发出的SIGALRM信号，然后执行回调函数sig_handler，向epoll中写信号信息
    struct sigaction sigact;
    memset(&sigact, '\0', sizeof(sigact));
    sigact.sa_handler=handler;
    if (restart){
        sigact.sa_flags |= SA_RESTART;
    }       
    //将所有信号添加到信号集中
    sigfillset(&sigact.sa_mask);
    //执行sigaction函数,收到SIGALRM信号（通过主程序alarm()函数发出）则执行handler（handler主要是往管道里写SIGALRM信号）
    assert(sigaction(sig, &sigact, NULL)!=-1);

}

void utils::show_error(int connfd, const char *info){
    send(connfd, info, strlen(info), 0);
};
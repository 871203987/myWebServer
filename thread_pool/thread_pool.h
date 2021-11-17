#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include <list>
#include <iostream>

#include "../locker/locker.h"
#include "../timer/timer.h"

template <class T>
class thread_pool{

private:
    int m_threadNums;
    int m_maxRequestNums;
    int m_isReactorMode;
    pthread_t *m_threadList;
    std::list<T*> m_taskList;
    locker m_locker;
    sem m_sem;
    timer_list *m_timerList;
    client_data *m_clients_timer;   //用户定时器数组
    

public:
    thread_pool(timer_list *timerList,client_data *clients_timer, int isReactorMode,int threadNums=8, int maxRequestNums=1000);
    ~thread_pool();
    bool addTask(T *request, int state);//state表示：0为读请求，1为写请求

private:
    static void* worker(void *arg);
    void run();

};

template<class T>
thread_pool<T>::thread_pool(timer_list *timerList, client_data *clients_timer, int isReactorMode,int threadNums, int maxRequestNums)
:m_timerList(timerList),
m_clients_timer(clients_timer),
m_threadNums(threadNums),
m_maxRequestNums(maxRequestNums),
m_isReactorMode(isReactorMode),
m_threadList(NULL){
    if (m_threadNums <= 0 || m_maxRequestNums <= 0)
        throw std::exception();
    
    m_threadList=new pthread_t[m_threadNums];
    if (!m_threadList)
        throw std::exception();
    for(int i=0;i<m_threadNums;++i){
        if (pthread_create(m_threadList + i, NULL, worker, this) != 0)
        {
            delete[] m_threadList;
            throw std::exception();
        }
        if (pthread_detach(m_threadList[i]))
        {
            delete[] m_threadList;
            throw std::exception();
        }
    }
};

template<class T>
thread_pool<T>::~thread_pool(){
    if(m_threadList)delete[] m_threadList;
}


template<class T>
bool thread_pool<T>::addTask(T *request, int state){
    if(!request){
        return false;
    }
    m_locker.lock();
    if(m_taskList.size()>=m_maxRequestNums){
        m_locker.unlock();
        return false;
    }
    request->m_state=state;
    m_taskList.push_back(request);
    m_locker.unlock();
    m_sem.post();
    return true;
}

template<class T>
void* thread_pool<T>::worker(void *arg){
    thread_pool *pool=static_cast<thread_pool*>(arg);

    pool->run();

    return pool;
}

template<class T>
void thread_pool<T>::run(){
    while(true){
        m_sem.wait();
        m_locker.lock();
        if(m_taskList.empty()){
            m_locker.unlock();
            continue;
        }
        T *request=m_taskList.front();
        m_taskList.pop_front();
        m_locker.unlock();
        if(m_isReactorMode==1){//reator模式
            if(request->m_state==0){//读请求
                if(request->read_once()){
                    // request->isdeal=1;
                    request->process();
                }
                else{//处理失败，关闭该链接
                    // request->isdeal=1;
                    // request->istimeout=1;
                    timer *tim=(m_clients_timer+request->get_sockfd())->client_timer;
                    if(tim)m_timerList->remove_timer(tim);
                    request->close_conn();
                }
            }
            else if(request->m_state==1){//写请求
                if(request->write()){
                    // request->isdeal=1;
                }
                else{//处理失败，关闭该链接
                    // request->isdeal=1;
                    // request->istimeout=1;
                    timer *tim=(m_clients_timer+request->get_sockfd())->client_timer;
                    if(tim)m_timerList->remove_timer(tim);
                    request->close_conn();
                }
            }
        }
        else{//proactor模式
            request->process();
        }
    }
}

#endif
#include "thread_pool.h"

template<class T>
thread_pool<T>::thread_pool(int isReactorMode,int threadNums, int maxRequestNums, )
:m_threadNums(threadNums),
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
                    request->process();
                    request->isdeal=1;
                }
                else{//处理失败，关闭该链接
                    request->isdeal=1;
                    request->istimeout=1;
                }
            }
            else if(request->m_state==1){//写请求
                if(request->write()){
                    request->isdeal=1;
                }
                else{//处理失败，关闭该链接
                    request->isdeal=1;
                    request->istimeout=1;
                }
            }
        }
        else{//proactor模式
            request->process();
        }
    }
}

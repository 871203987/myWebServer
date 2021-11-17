#include "timer.h"

timer::timer(){
    prev=NULL;
    next=NULL;
};
timer::~timer(){};

timer_list::timer_list(){
    head = NULL;
    tail = NULL;
};
timer_list::~timer_list(){
    timer *tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
};

void timer_list::add_timer(timer *tim){
    if (!tim)
    {
        return;
    }
    if (!head) 
    {
        head = tail = tim;
        return;
    }
    //如果新的定时器超时时间小于当前头部结点,直接将当前定时器结点作为头部结点
    if (tim->expireTime < head->expireTime)
    {
        tim->next = head;
        head->prev = tim;
        head = tim;
        return;
    }
    add_timer(tim, head);
};

void timer_list::add_timer(timer *tim, timer *list_head){
    timer *prev = list_head;
    timer *tmp = prev->next;
    while (tmp)
    {
        if (tim->expireTime < tmp->expireTime)
        {
            prev->next = tim;
            tim->next = tmp;
            tmp->prev = tim;
            tim->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    if (!tmp)
    {
        prev->next = tim;
        tim->prev = prev;
        tim->next = NULL;
        tail = tim;
    }
};

void timer_list::remove_timer(timer *tim){
    if (!tim)
    {
        return;
    }
    if ((tim == head) && (tim == tail))
    {
        delete tim;
        tim=NULL;
        head = NULL;
        tail = NULL;
        return;
    }
    if (tim == head)
    {
        head = head->next;
        head->prev = NULL;
        delete tim;
        tim=NULL;
        return;
    }
    if (tim == tail)
    {
        tail = tail->prev;
        tail->next = NULL;
        delete tim;
        tim=NULL;
        return;
    }
    tim->prev->next = tim->next;
    tim->next->prev = tim->prev;
    delete tim;
    tim=NULL;
};
void timer_list::adjust_timer(timer *tim){//调整定时器，任务发生变化时，调整定时器在链表中的位置,一般是时间延长
    if (!tim)
    {
        return;
    }
    timer *tmp = tim->next;
    if (!tmp || (tim->expireTime < tmp->expireTime))//被调整的定时器在链表尾部//定时器超时值仍然小于下一个定时器超时值，不调整
    {
        return;
    }
    if (tim == head)//被调整定时器是链表头结点，将定时器取出，重新插入
    {
        head = head->next;
        head->prev = NULL;
        tim->next = NULL;
        add_timer(tim, head);
    }
    else//被调整定时器在内部，将定时器取出，重新插入
    {
        tim->prev->next = tim->next;
        tim->next->prev = tim->prev;
        add_timer(tim, tim->next);
    }
};

void timer_list::tick(){
    if (!head)
    {
        return;
    }
    
    time_t cur = time(NULL);//获取当前时间
    timer *tmp = head;
    while (tmp)//遍历定时器链表
    {
        if (cur < tmp->expireTime)//当前时间小于定时器的超时时间，后面的定时器也没有到期
        {
            break;
        }
        tmp->callback_func(tmp->c_data);//当前定时器到期，则调用回调函数，取消注册，关闭连接
        head = tmp->next;//将处理后的定时器从链表容器中删除，并重置头结点
        if (head)
        {
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
};

void callback_func(client_data *user_data)
{
    epoll_ctl(http_conn::m_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_client_count--;
    LOG_INFO("close client(%s)", inet_ntoa(user_data->address.sin_addr));
}
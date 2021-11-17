#ifndef TIMER_H
#define TIMER_H

#include <arpa/inet.h>
#include <iostream>
#include <assert.h>

#include "../http_conn/http_conn.h"
#include "../log/log.h"

class timer;

struct  client_data
{
    sockaddr_in address;
    int sockfd;
    timer* client_timer;
};

class timer{
public:
    timer();
    ~timer();

    void (* callback_func)(client_data *);

public:
    time_t expireTime;

    client_data* c_data;

    timer* prev;
    timer* next;
};

class timer_list{
public:
    timer_list();
    ~timer_list();

    void add_timer(timer *tim);
    void remove_timer(timer *tim);
    void adjust_timer(timer *tim);

    void tick();
private:
    void add_timer(timer *tim, timer *list_head);

    timer* head;//头结点
    timer* tail;//尾结点

};


void callback_func(client_data *user_data);
#endif
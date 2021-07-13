#ifndef LST_TIMER
#define LST_TIMER

#include <time.h>
//#include "../log/log.h"
//定时器类前向声明
class util_timer;

//客户端数据结构体，因为服务端给客户端分配线程的时候要配套定时器
struct client_data{
    sockaddr_in address;    //客户端地址
    int sockfd;             //客户端文件描述符
    util_timer *timer;      //定时器
};

class util_timer{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire;                  //到期时间，是1970-01-01 00：00：00 到现在的秒数
    void (*cb_func)(client_data *);
    client_data *user_data;         //客户端数据
    util_timer *prev;               //前向指针
    util_timer *next;               //后向指针
};

// 定时器类，升序双向链表
class sort_timer_lst{
public:
    sort_timer_lst() : head(NULL), tail(NULL) {}
    ~sort_timer_lst(){
        util_timer *tmp = head;
        while (tmp){
            head = tmp->next;
            delete tmp;
            tmp = head;
        }
    }

    // 头插法，添加定时器进入链表
    void add_timer(util_timer *timer)
    {
        if (!timer){            //定时器为空
            return;
        }
        if (!head){             //定时链表为空，头尾都变成传入的timer
            head = tail = timer;
            return;
        }
        if (timer->expire < head->expire){      //定时器到期时间 < 链表到期时间
            timer->next = head;
            head->prev = timer;
            head = timer;
            return;
        }
        add_timer(timer, head); //还有一个同名函数，尾插法，在Head结点后某一位置
    }

    //调整定时链表顺序
    void adjust_timer(util_timer *timer)
    {
        if (!timer){
            return;
        }
        util_timer *tmp = timer->next;              //tmp为传入定时器后方定时器
        if (!tmp || (timer->expire < tmp->expire)){ //后面的到期时间大，不用调整
            return;
        }
        if (timer == head){
            head = head->next;
            head->prev = NULL;
            timer->next = NULL;
            add_timer(timer, head);
        }
        else{
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            add_timer(timer, timer->next);
        }
    }

    void del_timer(util_timer *timer)
    {
        if (!timer){
            return;
        }
        if ((timer == head) && (timer == tail)){
            delete timer;
            head = NULL;
            tail = NULL;
            return;
        }
        if (timer == head){
            head = head->next;
            head->prev = NULL;
            delete timer;
            return;
        }
        if (timer == tail){
            tail = tail->prev;
            tail->next = NULL;
            delete timer;
            return;
        }
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        delete timer;
    }

    // 计时
    void tick()
    {
        if (!head)
        {
            return;
        }
        //printf( "timer tick\n" );
        //LOG_INFO("%s", "timer tick");
        //Log::get_instance()->flush();
        time_t cur = time(NULL);        //获得标准时间
        util_timer *tmp = head;
        // 从头节点开始依次处理每个定时器，直到遇到一个尚未到期的定时器
        while (tmp)
        {
            /* 因为每个定时器都使用绝对时间作为超时值，所以可以把定时器的超时值和系统当前时间，
            比较以判断定时器是否到期*/
            if (cur < tmp->expire){
                break;
            }
            // 调用定时器的回调函数，以执行定时任务
            tmp->cb_func(tmp->user_data);
            // 执行完定时器中的定时任务之后，就将它从链表中删除，并重置链表头节点
            head = tmp->next;
            if (head){
                head->prev = NULL;
            }
            delete tmp;
            tmp = head;
        }
    }

private:
    void add_timer(util_timer *timer, util_timer *lst_head)
    {
        util_timer *prev = lst_head;
        util_timer *tmp = prev->next;
        while (tmp){
            if (timer->expire < tmp->expire){   // 不断遍历到timer合适的位置，到期时间小于后方结点
                prev->next = timer;
                timer->next = tmp;
                tmp->prev = timer;
                timer->prev = prev;
                break;
            }
            prev = tmp;
            tmp = tmp->next;
        }
        if (!tmp){                  // timer最大，成为尾结点
            prev->next = timer;
            timer->prev = prev;
            timer->next = NULL;
            tail = timer;
        }
    }

private:
    util_timer *head;
    util_timer *tail;
};

#endif

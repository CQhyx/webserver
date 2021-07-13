#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <cassert>

#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#include "lst_timer.h"

#define MAX_FD 65536   // 最大的文件描述符个数
#define MAX_EVENT_NUMBER 10000  // 监听的最大的事件数量
#define TIMESLOT 5             //最小超时单位

// 添加文件描述符
extern void addfd( int epollfd, int fd, bool one_shot );
extern void removefd( int epollfd, int fd );
extern int setnonblocking(int fd);

//添加代码
//设置定时器相关参数
static int pipefd[2];
static sort_timer_lst timer_lst;
static int epollfd = 0;

//信号处理函数
void sig_handler(int sig)
{
    //为保证函数的可重入性，保留原来的errno
    int save_errno = errno;
    int msg = sig;
    send(pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}
//定时处理任务，重新定时以不断触发SIGALRM信号
void timer_handler()
{
    timer_lst.tick();
    alarm(TIMESLOT);
}
//定时器回调函数，删除非活动连接在socket上的注册事件，并关闭
void cb_func(client_data *user_data)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0); //删除epoll事件
    assert(user_data);
    close(user_data->sockfd); //关闭客户端文件描述符，应该是
    http_conn::m_user_count--;
    //LOG_INFO("close fd %d", user_data->sockfd);
    //Log::get_instance()->flush();
}

//设置信号函数
void addsig(int sig, void( handler )(int)){
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    sa.sa_flags |= SA_RESTART;
    sigfillset( &sa.sa_mask ); // 阻塞信号集
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

int main( int argc, char* argv[] ) {
    
    if( argc <= 1 ) {
        printf( "usage: %s port_number\n", basename(argv[0]));
        return 1;
    }

    int port = atoi( argv[1] );
    // 忽略SIGPIPE信号
    // SIGPIPE产生的原因，客户端程序向服务器端程序发送了消息，然后关闭客户端，服务器端返回消息的时候就会收到内核给的SIGPIPE信号。
    addsig( SIGPIPE, SIG_IGN );

    // 创建线程池，初始化线程池   http_conn是任务类 + 客户端信息
    threadpool< http_conn >* pool = NULL;
    try {
        pool = new threadpool<http_conn>;
    } catch( ... ) {
        return 1;
    }

    // 创建一个数组用于保存所有的客户端信息
    http_conn* users = new http_conn[ MAX_FD ];

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );

    int ret = 0;
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = AF_INET;
    address.sin_port = htons( port );

    // 端口复用
    int reuse = 1;
    setsockopt( listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );
    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    ret = listen( listenfd, 5 );

    // 创建epoll对象，和事件数组
    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );
    // 把监听描述符添加到epoll对象中
    addfd( epollfd, listenfd, false );
    http_conn::m_epollfd = epollfd;

    //创建管道
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false);
    // 创建定时器数组
    client_data *users_timer = new client_data[MAX_FD];
    // 创建ALRM和TERM信号
    addsig(SIGALRM, sig_handler);
    addsig(SIGTERM, sig_handler);
    bool stop_server = false;

    bool timeout = false;
    alarm(TIMESLOT);

    while(true) {
        
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );
        
        if ( ( number < 0 ) && ( errno != EINTR ) ) {
            printf( "epoll failure\n" );
            break;
        }
        // 循环遍历事件数组
        for ( int i = 0; i < number; i++ ) {
            
            int sockfd = events[i].data.fd;
            
            if( sockfd == listenfd ) {
                // 表示有客户的连接进来
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );
                
                if ( connfd < 0 ) {
                    printf( "errno is: %d\n", errno );
                    continue;
                } 
                // 连接数已满
                if( http_conn::m_user_count >= MAX_FD ) {
                    close(connfd);
                    continue;
                }
                // 将新的客户数据初始化，放入数组中
                users[connfd].init( connfd, client_address);

                //初始化client_data数据
                //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd = connfd;
                util_timer *timer = new util_timer;
                timer->user_data = &users_timer[connfd];
                timer->cb_func = cb_func;
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                users_timer[connfd].timer = timer;
                timer_lst.add_timer(timer);

            } else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) ) {
                // 对方异常断开或者错误等事件
                // 服务器端关闭连接，移除对应的定时器
                users[sockfd].close_conn();
                util_timer *timer = users_timer[sockfd].timer;
                timer->cb_func(&users_timer[sockfd]);
                if (timer)
                {
                    timer_lst.del_timer(timer);
                }

            }else if((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)){ //读管道检测到了变化
                //处理信号
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1) { continue;}
                else if (ret == 0) { continue;}
                else{
                    for (int i = 0; i < ret; ++i){ // ret 实际copy的字节数，一个信号一个字节
                        switch (signals[i]){
                            case SIGALRM:{
                                // 用timeout变量标记有定时任务需要处理，但不立即处理定时任务
                                // 这是引文定时任务的优先级不如IO高
                                timeout = true; 
                                break;
                            }
                            case SIGTERM:{
                                stop_server = true;
                            }
                        }
                    }
                }
            } 
            else if(events[i].events & EPOLLIN) {
                util_timer *timer = users_timer[sockfd].timer; //文件描述符对应的定时器
                if(users[sockfd].read()) {
                    // 一次性把所有数据都读完
                    pool->append(users + sockfd);
                    //若有数据传输，则将定时器往后延迟3个单位
                    //并对新的定时器在链表上的位置进行调整
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        //LOG_INFO("%s", "adjust timer once");
                        //Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                } else {
                    users[sockfd].close_conn();
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }

            }  else if( events[i].events & EPOLLOUT ) {
                util_timer *timer = users_timer[sockfd].timer;
                if( users[sockfd].write() ) {
                    // 一次性写完所有数据
                    if (timer)
                    {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        //LOG_INFO("%s", "adjust timer once");
                        //Log::get_instance()->flush();
                        timer_lst.adjust_timer(timer);
                    }
                }else
                {
                    users[sockfd].close_conn();
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer)
                    {
                        timer_lst.del_timer(timer);
                    }
                }

            }
        }
        if (timeout)
        {
            timer_handler();
            timeout = false;
        }
    }
    
    close( epollfd );
    close( listenfd );
    close(pipefd[1]);
    close(pipefd[0]);
    delete [] users;
    delete[] users_timer;
    delete pool;
    return 0;
}
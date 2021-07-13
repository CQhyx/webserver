#ifndef LOCKER_H
#define LOCKER_H
#include <pthread.h>
#include <exception>
#include <semaphore.h>
using namespace std;
// 类的声明（包括类里面的成员和方法）、函数原型、常数

// 线程同步机制封装类

// 互斥锁类
class locker{
public:
    locker(){
        if(pthread_mutex_init(&m_mutex, NULL) != 0){
            throw exception();
        }
    }

    ~locker(){
        pthread_mutex_destroy(&m_mutex);
    }

    bool lock(){
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    bool unlock(){
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    // 获取互斥量
    pthread_mutex_t * get(){
        return &m_mutex;
    }
private:
    pthread_mutex_t m_mutex;
};


// 条件变量类
class cond {
public:
    cond(){
        if(pthread_cond_init(&m_cond, NULL) != 0){
            throw exception();
        }
    }
    ~cond(){
        pthread_cond_destroy(&m_cond);
    }
    // 搭配互斥锁使用
    bool wait(pthread_mutex_t *mutex){
        return pthread_cond_wait(&m_cond, mutex) == 0;
    }
    //等待多长时间，调用了这个函数，线程会阻塞，直到指定的时间t结束。
    bool timedwait(pthread_mutex_t *mutex, struct timespec t){
        return pthread_cond_timedwait(&m_cond, mutex, &t) == 0;
    }
    //唤醒一个或者多个等待的线程
    bool signal(){
        return pthread_cond_signal(&m_cond) == 0;
    }
    //唤醒所有的等待的线程
    bool broadcast(){
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    pthread_cond_t m_cond;
};

// 信号量类
class sem {
public:
    sem(){
        if(sem_init(&m_sem, 0, 0) != 0){
            throw exception();
        }
    }
    sem(int num){
        if(sem_init(&m_sem, 0, num) != 0){
            throw exception();
        }
    }
    ~sem(){
        sem_destroy(&m_sem);
    }
    // 等待信号量， 对信号量加锁，调用一次对信号量的值-1，如果值为0，就阻塞
    bool wait() {
        return sem_wait( &m_sem ) == 0;
    }
    // 增加信号量， 对信号量解锁，调用一次对信号量的值+1
    bool post() {
        return sem_post( &m_sem ) == 0;
    }
private:
    sem_t m_sem;
};


#endif
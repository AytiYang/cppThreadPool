#ifndef _THREADPOOL_H
#define _THREADPOOL_H

#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>



typedef struct Task
{
    void (*function)(void* arg);
    void* arg;
}Task;

typedef struct threadPool
{
    Task* taskQ;
    int queueCapacity;
    int queueSize;
    int queueFront;
    int queueRear;

    pthread_t managerID;
    pthread_t* threadIDs;

    int minNum; //最小线程数
    int maxNum; //最大线程数
    int busyNum;//忙线程
    int liveNum;//存活线程
    int exitNum;//要销毁的线程

    pthread_mutex_t mutexPool;//锁整个线程池
    pthread_mutex_t mutexBusy;//锁 忙线程 变量

    int shutdownn;//是否需要销毁线程池

    pthread_cond_t notFull;
    pthread_cond_t notEmpty;

    
}threadPool;

//创建线程池and init
threadPool* threadPoolCreate(int min,int max,int queueSize);

//销毁线程池
int threadPoolDestory(threadPool* pool);


//添加任务
void threadPoolAdd(threadPool* pool,void(*func)(void*),void* arg);

//获取线程池工作线程个数
int threadPoolBusyNum(threadPool* pool);
//获取线程池中活着的线程的个数
int threadPoolLiveNum(threadPool* pool);


void *worker(void* arg);
void *manager(void* arg);
void threadExit(threadPool* pool);
#endif
